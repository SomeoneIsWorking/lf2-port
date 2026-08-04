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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

typedef struct {
    uint32_t pixels;      /* guest address of the PCM data */
    uint32_t bytes;
    int      playing, looping;
    uint32_t pos;         /* play cursor in bytes -- what the guest sees */
    /* The authoritative cursor, in FRACTIONAL source frames. `pos` cannot hold it: a
     * buffer whose rate differs from the device advances by a non-integer step, and
     * rounding that into a byte offset once per callback resets the phase on every pull.
     * That is why only the 22050 Hz sounds (step exactly 1.0) came out right. */
    double   cur;
    int      channels, rate, bits;
    int      volume;      /* millibels, 0 = full */
    int      dumped;      /* LF2_AUDIO_DUMP_SRC: written once, at first Unlock */
} SBuf;

enum { MAX_BUFS = 128 };
static SBuf *bufs[MAX_BUFS];
static int nbufs;

enum { MIX_RATE = 22050, MIX_CHANNELS = 2 };
enum { MIX_SLICE = 4096 };        /* frames mixed per pass; a pull may need several */

static SDL_AudioStream *stream;
static SDL_Mutex *mix_lock;

/* LF2_AUDIO_DEBUG. Four counters rather than one, because they fail independently: the
 * game may never create a buffer, never start one, never have the device pull from us, or
 * pull and get silence. A single "audio works" flag cannot tell those apart, and peak
 * amplitude is the only one of them that proves sound would actually be heard. */
long au_bufs, au_plays, au_pulls, au_peak, au_clipped, au_samples;
long au_multislice_pulls, au_would_drop_frames, au_max_req;
/* Buffers the game created past MAX_BUFS. They are not in bufs[], so the mixer
 * never sees them and the sound is silently absent -- indistinguishable from a
 * dropout unless it is counted. */
long au_unregistered;

static FILE *mix_dump;
static long  mix_dump_frames;

/* A WAV header needs the final length, so it is written as a placeholder and patched on
 * close. If the process dies without closing, the file is still playable by anything that
 * reads to EOF, but the sizes will read as zero -- so it says so rather than pretending. */
static void mix_dump_open(void)
{
    const char *path = getenv("LF2_AUDIO_DUMP_MIX");
    if (!path || !*path || mix_dump) return;
    /* Create the parent directories. A relative path here is resolved against the game
     * tree, because run.sh chdirs into it before exec -- so the obvious invocation
     * (LF2_AUDIO_DUMP_MIX=scratch/wav/mix.wav ./run.sh) aimed at a directory that does
     * not exist, fopen failed, and the run recorded nothing while looking fine. */
    {
        char dirs[512];
        snprintf(dirs, sizeof dirs, "%s", path);
        for (char *q = dirs + 1; *q; q++)
            if (*q == '/') { *q = 0; mkdir(dirs, 0777); *q = '/'; }
    }
    mix_dump = fopen(path, "wb");
    if (!mix_dump) {
        char cwd[512];
        /* Loud, and it names the resolved location -- "cannot write" without saying where
         * it tried is what made this look like a recording that simply came out empty. */
        fprintf(stderr,
                "\n*** audio dump FAILED: cannot open \"%s\"\n"
                "*** working directory is %s\n"
                "*** a relative path lands inside the game tree; use an absolute one.\n"
                "*** NOTHING WILL BE RECORDED.\n\n",
                path, getcwd(cwd, sizeof cwd) ? cwd : "?");
        return;
    }
    unsigned char hdr[44] = {0};
    memcpy(hdr, "RIFF", 4); memcpy(hdr + 8, "WAVEfmt ", 8); memcpy(hdr + 36, "data", 4);
    hdr[16] = 16; hdr[20] = 1; hdr[22] = MIX_CHANNELS;
    hdr[24] = (unsigned char)(MIX_RATE & 0xff); hdr[25] = (unsigned char)(MIX_RATE >> 8);
    const int abps = MIX_RATE * MIX_CHANNELS * 2;
    hdr[28] = (unsigned char)(abps & 0xff);       hdr[29] = (unsigned char)((abps >> 8) & 0xff);
    hdr[30] = (unsigned char)((abps >> 16) & 0xff); hdr[31] = (unsigned char)((abps >> 24) & 0xff);
    hdr[32] = MIX_CHANNELS * 2; hdr[34] = 16;
    fwrite(hdr, 1, sizeof hdr, mix_dump);
    fprintf(stderr, "audio dump: recording the mix to %s\n", path);
}

void mix_dump_close(void)
{
    if (!mix_dump) return;
    const uint32_t data = (uint32_t)mix_dump_frames * MIX_CHANNELS * 2u;
    const uint32_t riff = 36u + data;
    fseek(mix_dump, 4, SEEK_SET);  fwrite(&riff, 4, 1, mix_dump);
    fseek(mix_dump, 40, SEEK_SET); fwrite(&data, 4, 1, mix_dump);
    fclose(mix_dump);
    mix_dump = NULL;
    fprintf(stderr, "audio dump: wrote %ld frames (%.1f s)\n",
            mix_dump_frames, (double)mix_dump_frames / (double)MIX_RATE);
}

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

/* One sample of frame `i`, channel `ch`, as a float on the int16 scale. 8-bit PCM is
 * unsigned with 128 as silence, which is why it is biased before scaling. */
static float samp_at(const SBuf *b, uint32_t i, int ch)
{
    const uint32_t bps = (uint32_t)(b->bits / 8) * (uint32_t)b->channels;
    const uint32_t off = i * bps;
    const int sc = (ch < b->channels) ? ch : 0;
    if (b->bits == 8)
        return (float)(((int32_t)LD8(b->pixels + off + (uint32_t)sc) - 128) << 8);
    return (float)(int16_t)LD16(b->pixels + off + (uint32_t)(sc * 2));
}

/* Additive mix of every playing buffer. Buffers are 8- or 16-bit mono/stereo at their own
 * rate, so each is resampled to the device rate with a persistent fractional cursor and
 * linear interpolation. Both parts matter: the cursor has to survive across callbacks or
 * the phase resets on every pull, and nearest-neighbour picking made non-22050 content
 * sound wrong even when its average rate was right. */
static void mix_slice(SDL_AudioStream *s, int chunk)
{
    static int16_t out[MIX_SLICE * MIX_CHANNELS];
    memset(out, 0, (size_t)chunk * sizeof(int16_t) * MIX_CHANNELS);

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
        const uint32_t nframes = b->bytes / (uint32_t)bps;
        if (!nframes) continue;

        for (int f = 0; f < chunk; f++) {
            if (b->cur < 0) b->cur = 0;
            uint32_t i0 = (uint32_t)b->cur;
            if (i0 + 1 >= nframes) {
                /* Out of source material for an interpolated pair. */
                if (!b->looping) { b->playing = 0; break; }
                b->cur -= (double)nframes;
                if (b->cur < 0) b->cur = 0;
                i0 = (uint32_t)b->cur;
                if (i0 + 1 >= nframes) break;      /* buffer shorter than one pair */
            }
            const float frac = (float)(b->cur - (double)i0);
            for (int c = 0; c < MIX_CHANNELS; c++) {
                const float v = samp_at(b, i0, c) * (1.0f - frac)
                              + samp_at(b, i0 + 1, c) * frac;
                int32_t acc = out[f * MIX_CHANNELS + c] + (int32_t)(v * gain);
                out[f * MIX_CHANNELS + c] = (int16_t)(acc > 32767 ? 32767 :
                                                      acc < -32768 ? -32768 : acc);
            }
            b->cur += step;
        }
        b->pos = (uint32_t)b->cur * (uint32_t)bps;   /* what the guest polls */
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

    /* LF2_AUDIO_DUMP_MIX=<path.wav> records exactly what goes to the device.
     *
     * Every stage before this point has been verified in isolation -- the PCM matches the
     * files on disk byte for byte, the formats match their headers, and the resampler has
     * unit tests with a control that fails on the old implementation. When the parts are
     * all correct and the whole still sounds wrong, the only thing left to examine is the
     * output itself, and it has to come from a real session rather than a scripted route
     * that fires six sounds. The header is patched at exit by mix_dump_close(). */
    if (mix_dump) {
        fwrite(out, sizeof(int16_t) * MIX_CHANNELS, (size_t)chunk, mix_dump);
        mix_dump_frames += (long)chunk;
    }

    SDL_PutAudioStreamData(s, out, chunk * (int)sizeof(int16_t) * MIX_CHANNELS);
}

/* SDL asks for `additional` bytes and expects all of them. The previous version mixed at
 * most one 4096-frame slice per pull and returned whatever that produced, so any larger
 * request was silently short-changed and the stream starved -- audible as dropouts. */
static void SDLCALL feed(void *ud, SDL_AudioStream *s, int additional, int total)
{
    (void)ud; (void)total;
    if (additional <= 0) return;
    const int frames = additional / (int)(sizeof(int16_t) * MIX_CHANNELS);
    au_pulls++;
    if (frames > au_max_req) au_max_req = frames;
    if (frames > MIX_SLICE) { au_multislice_pulls++; au_would_drop_frames += frames - MIX_SLICE; }
    for (int done = 0; done < frames; ) {
        const int chunk = (frames - done) > MIX_SLICE ? MIX_SLICE : (frames - done);
        mix_slice(s, chunk);
        done += chunk;
    }
}

static void audio_start(void)
{
    if (stream) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    mix_lock = SDL_CreateMutex();
    mix_dump_open();
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

static void dump_src(const SBuf *b);

/* Dumped here rather than at Play: the game writes the PCM under Lock/Unlock, and only a
 * handful of buffers are ever played in a scripted route, so dumping at Play would show
 * exactly the sounds that already work and none of the ones being investigated. */
static void sb_Unlock(uint32_t self)
{
    SBuf *b = com_host(self);
    if (b && !b->dumped) { b->dumped = 1; dump_src(b); }
    com_ret(5, DD_OK);
}

/* LF2_AUDIO_DUMP_SRC=<dir> writes each buffer's PCM, exactly as the game left it, as a
 * WAV in the buffer's own declared format -- the moment before it is first played.
 *
 * This is the bisect between "the game handed us bad samples" and "we play good samples
 * badly". The mixer is unit-tested and the WAVEFORMATEX parse matches the files on disk,
 * so if these dumps match the WAV files under data/ the fault is in playback, and if they
 * do not, the fault is upstream in mmio/Lock and the mixer was never involved. */
static void dump_src(const SBuf *b)
{
    const char *dir = getenv("LF2_AUDIO_DUMP_SRC");
    if (!dir || !*dir || !b->bytes) return;
    static int n;
    char path[512];
    snprintf(path, sizeof path, "%s/buf_%03d_%dHz_%dch_%dbit.wav",
             dir, n++, b->rate, b->channels, b->bits);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "audio dump: cannot write %s\n", path); return; }

    const uint32_t data = b->bytes;
    const uint16_t ch = (uint16_t)b->channels, bits = (uint16_t)b->bits;
    const uint32_t rate = (uint32_t)b->rate;
    const uint16_t align = (uint16_t)(ch * (bits / 8));
    const uint32_t abps = rate * align;
    const uint32_t riff = 36 + data;
    const uint16_t fmt_tag = 1;
    const uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_len, 4, 1, f);
    fwrite(&fmt_tag, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&abps, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(g_mem + b->pixels, 1, data, f);
    fclose(f);
}

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
    /* The guest owns `pos`; the mixer runs off `cur`. Resync or the seek is ignored. */
    const int bps = (b->bits / 8) * b->channels;
    b->cur = bps > 0 ? (double)b->pos / (double)bps : 0.0;
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
    else au_unregistered++;   /* created but never mixed: it plays as silence */
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
    b->cur = 0.0;
    if (nbufs < MAX_BUFS) { bufs[nbufs++] = b; au_bufs++; }
    else au_unregistered++;   /* created but never mixed: it plays as silence */
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
    /* Deliberately NOT prefixed "audio:" -- tools/smoke_test.sh takes the last line with
     * that prefix, so a second one silently blanked every audio assertion. */
    fprintf(stderr, "mixer: multi-slice pulls=%ld (%ld frames the old single-slice mixer "
                    "would have dropped), max request=%ld frames, slice=%d\n",
            au_multislice_pulls, au_would_drop_frames, au_max_req, MIX_SLICE);
    fprintf(stderr, "mixer: buffers unregistered past MAX_BUFS=%d: %ld (each one plays as silence)\n", MAX_BUFS, au_unregistered);
    if (!au_multislice_pulls && au_pulls)
        fprintf(stderr, "  no under-delivery: every pull was satisfied in full\n");
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
