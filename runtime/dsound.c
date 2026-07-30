/* DirectSound on SDL3.
 *
 * Buffers live in guest memory so Lock() returns a plain address and the game writes
 * PCM straight into it, which is what it does. Mixing is a simple additive S16 mix of
 * every playing buffer, driven from an SDL audio stream callback. */
#include "com.h"
#include "guest_ops.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

typedef struct {
    uint32_t pixels;      /* guest address of the PCM data */
    uint32_t bytes;
    int      playing, looping;
    uint32_t pos;         /* play cursor, in bytes */
    int      channels, rate, bits;
    int      volume;      /* millibels, 0 = full */
} SBuf;

enum { MAX_BUFS = 128 };
static SBuf *bufs[MAX_BUFS];
static int nbufs;

enum { MIX_RATE = 22050, MIX_CHANNELS = 2 };

static SDL_AudioStream *stream;
static SDL_Mutex *mix_lock;

/* LF2_AUDIO_DEBUG. Four counters rather than one, because they fail independently: the
 * game may never create a buffer, never start one, never have the device pull from us, or
 * pull and get silence. A single "audio works" flag cannot tell those apart, and peak
 * amplitude is the only one of them that proves sound would actually be heard. */
long au_bufs, au_plays, au_pulls, au_peak, au_clipped, au_samples;

/* ---- background music ----
 * The game's BGM is WMA, which nothing here decodes. Rather than ship a decoder, the
 * track is decoded to raw PCM by whatever ffmpeg is on PATH, once, at RenderFile time.
 * That keeps it an optional runtime dependency: no ffmpeg means no music and a clear
 * message, never a broken build. The whole track is decoded up front -- roughly 16 MB for
 * three minutes at this rate -- which avoids feeding a pipe from the audio callback.
 */
static void audio_start(void);    /* defined below; music must be able to open the device */

static int16_t *mus_pcm;          /* interleaved stereo at MIX_RATE */
static size_t   mus_frames, mus_pos;
static int      mus_playing;
static float    mus_gain = 0.5f;  /* until the game says otherwise */
long            au_music_frames;

/* IBasicAudio volume is hundredths of a dB, -10000 (silence) to 0 (unattenuated), the
 * same scale DirectSound uses for effects. */
void music_set_volume(int32_t centibels)
{
    mus_gain = (centibels <= -10000) ? 0.0f : SDL_powf(10.0f, (float)centibels / 2000.0f);
}

void music_stop(void) { mus_playing = 0; }

/* Opens the device itself. It was previously only opened when a sound *effect* first
 * played, so music alone -- which is all that happens on the menus -- produced a decoded
 * track that nothing ever pulled. */
void music_start(void)
{
    if (!mus_pcm) return;
    audio_start();
    mus_playing = 1;
    mus_pos = 0;
}

/* Returns 0 and explains itself on any failure; never leaves a half-loaded track.
 *
 * fork/exec rather than popen: the path comes from the game's data files, and building a
 * shell command string around it would make an attacker-supplied filename a shell
 * injection. Quoting it would be a patch over the wrong mechanism -- there is no reason
 * for a shell to be involved at all. */
static FILE *ffmpeg_open(const char *path, pid_t *out_pid)
{
    int fd[2];
    if (pipe(fd) != 0) return NULL;

    char rate[16], chans[8];
    snprintf(rate, sizeof rate, "%d", MIX_RATE);
    snprintf(chans, sizeof chans, "%d", MIX_CHANNELS);

    const pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return NULL; }
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp("ffmpeg", "ffmpeg", "-v", "quiet", "-nostdin", "-i", path,
               "-f", "s16le", "-acodec", "pcm_s16le", "-ar", rate, "-ac", chans,
               "-", (char *)NULL);
        _exit(127);                      /* exec failed: no ffmpeg on PATH */
    }
    close(fd[1]);
    *out_pid = pid;
    return fdopen(fd[0], "r");
}

int music_load(const char *path)
{
    music_stop();
    free(mus_pcm); mus_pcm = NULL; mus_frames = 0; au_music_frames = 0;

    pid_t pid = -1;
    FILE *pipe = ffmpeg_open(path, &pid);
    if (!pipe) { fprintf(stderr, "music: could not start ffmpeg\n"); return 0; }

    size_t cap = 1u << 20, len = 0;
    int16_t *buf = malloc(cap);
    if (!buf) { fclose(pipe); waitpid(pid, NULL, 0); return 0; }
    for (;;) {
        if (len == cap) {
            int16_t *bigger = realloc(buf, cap * 2);
            if (!bigger) { free(buf); fclose(pipe); waitpid(pid, NULL, 0); return 0; }
            buf = bigger; cap *= 2;
        }
        const size_t got = fread((char *)buf + len, 1, cap - len, pipe);
        if (got == 0) break;
        len += got;
    }
    fclose(pipe);
    int status = 0;
    waitpid(pid, &status, 0);

    if (len == 0) {
        free(buf);
        const int exited = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        fprintf(stderr, "music: no audio decoded from %s%s\n", path,
                exited == 127 ? " (ffmpeg not found on PATH)" : "");
        return 0;
    }
    mus_pcm = buf;
    mus_frames = len / (sizeof(int16_t) * MIX_CHANNELS);
    au_music_frames = (long)mus_frames;
    return 1;
}


static float gain_of(const SBuf *b)
{
    if (b->volume <= -10000) return 0.0f;
    return SDL_powf(10.0f, (float)b->volume / 2000.0f);
}

/* Additive mix of every playing buffer. Buffers are 8- or 16-bit mono/stereo at their
 * own rate; stepping the source cursor by a ratio handles the resample crudely but
 * audibly, which is enough until the audio path is worth tuning. */
static void SDLCALL feed(void *ud, SDL_AudioStream *s, int additional, int total)
{
    (void)ud; (void)total;
    if (additional <= 0) return;
    const int frames = additional / (int)(sizeof(int16_t) * MIX_CHANNELS);
    static int16_t out[4096 * MIX_CHANNELS];
    const int chunk = frames > 4096 ? 4096 : frames;
    memset(out, 0, (size_t)chunk * sizeof(int16_t) * MIX_CHANNELS);

    au_pulls++;

    /* Music first, so the effect mixing below saturates against it the same way it does
     * against other effects. */
    if (mus_playing && mus_pcm) {
        for (int f = 0; f < chunk; f++) {
            if (mus_pos >= mus_frames) mus_pos = 0;            /* loop */
            for (int c = 0; c < MIX_CHANNELS; c++)
                out[f * MIX_CHANNELS + c] =
                    (int16_t)((float)mus_pcm[mus_pos * MIX_CHANNELS + c] * mus_gain);
            mus_pos++;
        }
    }

    SDL_LockMutex(mix_lock);
    for (int i = 0; i < nbufs; i++) {
        SBuf *b = bufs[i];
        if (!b || !b->playing || !b->bytes) continue;
        const int bps = (b->bits / 8) * b->channels;
        if (bps <= 0) continue;
        const float gain = gain_of(b);
        const double step = (double)b->rate / (double)MIX_RATE;
        double cursor = (double)b->pos / (double)bps;

        for (int f = 0; f < chunk; f++) {
            const uint32_t off = (uint32_t)cursor * (uint32_t)bps;
            if (off + (uint32_t)bps > b->bytes) {
                if (b->looping) { cursor = 0; continue; }
                b->playing = 0;
                break;
            }
            for (int c = 0; c < MIX_CHANNELS; c++) {
                const int sc = (c < b->channels) ? c : 0;
                int32_t v;
                if (b->bits == 8)
                    v = ((int32_t)LD8(b->pixels + off + (uint32_t)sc) - 128) << 8;
                else
                    v = (int16_t)LD16(b->pixels + off + (uint32_t)(sc * 2));
                int32_t acc = out[f * MIX_CHANNELS + c] + (int32_t)((float)v * gain);
                out[f * MIX_CHANNELS + c] = (int16_t)(acc > 32767 ? 32767 :
                                                      acc < -32768 ? -32768 : acc);
            }
            cursor += step;
        }
        b->pos = (uint32_t)cursor * (uint32_t)bps;
    }
    SDL_UnlockMutex(mix_lock);
    /* A peak that saturates cannot say how much it saturated by, so count the samples
     * that hit the rail as well. One clipped sample is inaudible; a steady percentage is
     * audible distortion. */
    for (int i = 0; i < chunk * MIX_CHANNELS; i++) {
        const long v = out[i] < 0 ? -(long)out[i] : (long)out[i];
        if (v > au_peak) au_peak = v;
        if (out[i] >= 32767 || out[i] <= -32768) au_clipped++;
        au_samples++;
    }

    SDL_PutAudioStreamData(s, out, chunk * (int)sizeof(int16_t) * MIX_CHANNELS);
}

static void audio_start(void)
{
    if (stream) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    mix_lock = SDL_CreateMutex();
    SDL_AudioSpec spec = { SDL_AUDIO_S16, MIX_CHANNELS, MIX_RATE };
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (stream) SDL_ResumeAudioStreamDevice(stream);
}

/* ---- IDirectSoundBuffer ---- */

static void sb_Lock(uint32_t self)
{
    SBuf *b = com_host(self);
    const uint32_t offset = ARG(1), bytes = ARG(2);
    const uint32_t p1 = ARG(3), s1 = ARG(4), p2 = ARG(5), s2 = ARG(6);
    uint32_t n = bytes ? bytes : b->bytes;
    if (offset + n > b->bytes) n = b->bytes > offset ? b->bytes - offset : 0;
    if (p1) ST32(p1, b->pixels + offset);
    if (s1) ST32(s1, n);
    if (p2) ST32(p2, 0);
    if (s2) ST32(s2, 0);
    com_ret(8, DD_OK);
}

static void sb_Unlock(uint32_t self) { (void)self; com_ret(5, DD_OK); }

static void sb_Play(uint32_t self)
{
    SBuf *b = com_host(self);
    audio_start();
    SDL_LockMutex(mix_lock);
    b->looping = (ARG(3) & 1) != 0;      /* DSBPLAY_LOOPING */
    b->playing = 1;
    au_plays++;
    SDL_UnlockMutex(mix_lock);
    com_ret(4, DD_OK);
}

static void sb_Stop(uint32_t self)
{
    SBuf *b = com_host(self);
    b->playing = 0;
    com_ret(1, DD_OK);
}

/* The write cursor must LEAD the play cursor. Returning the same value for both -- which
 * this did -- tells a streaming caller there is no room to write, and the game stalls: it
 * streams into a looping buffer, and against real DirectDraw/DirectSound it locks and
 * writes ~500 times a second, where this port managed five writes in total and then
 * stopped. Wine reports the pair as playpos 246960 / writepos 250488, a lead of 3528
 * bytes, which is 40 ms at the buffer's own rate; that is what is reproduced here. */
static void sb_GetCurrentPosition(uint32_t self)
{
    SBuf *b = com_host(self);
    if (!b) { com_ret(3, DD_OK); return; }

    const uint32_t bps = (uint32_t)(b->rate * b->channels * (b->bits / 8));
    uint32_t lead = bps ? bps * 40u / 1000u : 0u;      /* 40 ms of write-ahead */
    if (b->bytes && lead >= b->bytes) lead = b->bytes / 4;

    if (ARG(1)) ST32(ARG(1), b->pos);
    if (ARG(2)) ST32(ARG(2), b->bytes ? (b->pos + lead) % b->bytes : b->pos + lead);
    com_ret(3, DD_OK);
}

static void sb_SetCurrentPosition(uint32_t self)
{
    SBuf *b = com_host(self);
    b->pos = ARG(1);
    com_ret(2, DD_OK);
}

static void sb_GetStatus(uint32_t self)
{
    SBuf *b = com_host(self);
    uint32_t st = 0;
    if (b->playing) st |= 1;                     /* DSBSTATUS_PLAYING */
    if (b->looping) st |= 4;                     /* DSBSTATUS_LOOPING */
    if (ARG(1)) ST32(ARG(1), st);
    com_ret(2, DD_OK);
}

static void sb_SetVolume(uint32_t self)
{
    SBuf *b = com_host(self);
    b->volume = (int)ARG(1);
    com_ret(2, DD_OK);
}

static void sb_ret_ok1(uint32_t self) { (void)self; com_ret(1, DD_OK); }
static void sb_ret_ok2(uint32_t self) { (void)self; com_ret(2, DD_OK); }

static void obj_QueryInterface(uint32_t self) { ST32(ARG(2), self); com_ret(3, DD_OK); }
static void obj_AddRef(uint32_t self)  { (void)self; com_ret(1, 1); }
static void obj_Release(uint32_t self) { (void)self; com_ret(1, 0); }

/* ---- IDirectSound ---- */

enum { PCM_ARENA = 0x60000000u };
static uint32_t pcm_next = PCM_ARENA;

static void ds_CreateSoundBuffer(uint32_t self)
{
    (void)self;
    const uint32_t desc = ARG(1), out = ARG(2);
    /* DSBUFFERDESC: dwSize +0, dwFlags +4, dwBufferBytes +8, dwReserved +12,
     * lpwfxFormat +16, guid3DAlgorithm +20. These were read at +12 and +20, i.e.
     * dwReserved and the GUID, so every buffer came out as the 4-byte fallback with the
     * default format -- 116 buffers all reporting "4 bytes, 22050 Hz 1ch 8bit" while the
     * oracle creates them at 7006, 34156, 41096, 144384, 352800 bytes. */
    const uint32_t bytes = LD32(desc + 8);
    const uint32_t wfx = LD32(desc + 16);

    SBuf *b = SDL_calloc(1, sizeof *b);
    b->bytes = bytes ? bytes : 4;
    b->pixels = pcm_next;
    pcm_next = (pcm_next + b->bytes + 4095u) & ~4095u;
    memset(g_mem + b->pixels, 0, b->bytes);
    b->channels = 1; b->rate = 22050; b->bits = 8;
    if (wfx) {
        b->channels = (int)LD16(wfx + 2);
        b->rate     = (int)LD32(wfx + 4);
        b->bits     = (int)LD16(wfx + 14);
        if (b->channels < 1 || b->channels > 2) b->channels = 1;
        if (b->rate < 4000 || b->rate > 192000) b->rate = 22050;
        if (b->bits != 8 && b->bits != 16) b->bits = 8;
    }
    if (nbufs < MAX_BUFS) { bufs[nbufs++] = b; au_bufs++; }
    if (getenv("LF2_AUDIO_DEBUG"))
        fprintf(stderr, "buffer created: %u bytes, %d Hz %dch %dbit\n",
                b->bytes, b->rate, b->channels, b->bits);
    ST32(out, com_create(IF_DSBUFFER, b));
    com_ret(4, DD_OK);
}

/* The game plays the same sound concurrently, so it duplicates buffers. Returning S_OK
 * without writing the out-pointer left the game calling through uninitialised memory. */
static void ds_DuplicateSoundBuffer(uint32_t self)
{
    (void)self;
    const uint32_t src = ARG(1), out = ARG(2);
    SBuf *o = src ? com_host(src) : NULL;
    if (!o || !out) { if (out) ST32(out, 0); com_ret(3, E_FAIL); return; }

    SBuf *b = SDL_calloc(1, sizeof *b);
    *b = *o;                     /* shares the PCM; its own play cursor */
    b->playing = 0;
    b->pos = 0;
    if (nbufs < MAX_BUFS) { bufs[nbufs++] = b; au_bufs++; }
    if (getenv("LF2_AUDIO_DEBUG"))
        fprintf(stderr, "buffer created: %u bytes, %d Hz %dch %dbit\n",
                b->bytes, b->rate, b->channels, b->bits);
    ST32(out, com_create(IF_DSBUFFER, b));
    com_ret(3, DD_OK);
}

static void ds_ret_ok2(uint32_t self) { (void)self; com_ret(2, DD_OK); }
static void ds_ret_ok3(uint32_t self) { (void)self; com_ret(3, DD_OK); }

void audio_report(void)
{
    fprintf(stderr, "audio: buffers=%ld plays=%ld device-pulls=%ld peak=%ld/32767 "
                    "clipped=%ld/%ld (%.3f%%) music-frames=%ld\n",
            au_bufs, au_plays, au_pulls, au_peak, au_clipped, au_samples,
            au_samples ? 100.0 * (double)au_clipped / (double)au_samples : 0.0,
            au_music_frames);
    if (!au_bufs)  fprintf(stderr, "  the game never created a sound buffer\n");
    else if (!au_plays) fprintf(stderr, "  buffers exist but none was ever started\n");
    else if (!au_pulls) fprintf(stderr, "  buffers played but the device never pulled -- "
                                        "the mixer callback is not running\n");
    else if (!au_peak)  fprintf(stderr, "  the mixer ran but produced pure silence, so "
                                        "nothing would be heard\n");
}

void dsound_register(void)
{
    ComClass *c = &com_class[IF_DSOUND];
    c->name = "IDirectSound";
    c->nmethods = 11;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[3] = ds_CreateSoundBuffer;
    c->method[4] = ds_ret_ok2;                   /* GetCaps */
    c->method[5] = ds_DuplicateSoundBuffer;
    c->method[6] = ds_ret_ok3;                   /* SetCooperativeLevel */
    c->method[7] = sb_ret_ok1;                   /* Compact */
    c->method[8] = ds_ret_ok2;                   /* GetSpeakerConfig */
    c->method[9] = ds_ret_ok2;                   /* SetSpeakerConfig */
    c->method[10] = ds_ret_ok2;                  /* Initialize */

    c = &com_class[IF_DSBUFFER];
    c->name = "IDirectSoundBuffer";
    c->nmethods = 21;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[3] = sb_ret_ok2;                   /* GetCaps */
    c->method[4] = sb_GetCurrentPosition;
    c->method[5] = ds_ret_ok3;                   /* GetFormat */
    c->method[6] = sb_ret_ok2;                   /* GetVolume */
    c->method[7] = sb_ret_ok2;                   /* GetPan */
    c->method[8] = sb_ret_ok2;                   /* GetFrequency */
    c->method[9] = sb_GetStatus;
    c->method[10] = sb_ret_ok2;                  /* Initialize */
    c->method[11] = sb_Lock;
    c->method[12] = sb_Play;
    c->method[13] = sb_SetCurrentPosition;
    c->method[14] = sb_ret_ok2;                  /* SetFormat */
    c->method[15] = sb_SetVolume;
    c->method[16] = sb_ret_ok2;                  /* SetPan */
    c->method[17] = sb_ret_ok2;                  /* SetFrequency */
    c->method[18] = sb_Stop;
    c->method[19] = sb_Unlock;
    c->method[20] = sb_ret_ok1;                  /* Restore */
}

/* DirectSoundCreate(guid, ppDS, outer) -- imported by ordinal #1. */
static void dsound_create(void)
{
    ST32(LD32(R(ESP) + 8), com_create(IF_DSOUND, NULL));
    R(EAX) = DD_OK;
    R(ESP) += 4 + 12;
}

typedef void (*Handler)(void);

Handler dsound_lookup(const char *dll, const char *name)
{
    if (strcmp(dll, "DSOUND.dll") == 0 && (strcmp(name, "#1") == 0 ||
                                           strcmp(name, "DirectSoundCreate") == 0))
        return dsound_create;
    return NULL;
}
