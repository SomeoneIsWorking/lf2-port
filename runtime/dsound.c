/* DirectSound on SDL3.
 *
 * Buffers live in guest memory so Lock() returns a plain address and the game writes
 * PCM straight into it, which is what it does. Mixing is a simple additive S16 mix of
 * every playing buffer, driven from an SDL audio stream callback. */
#include "com.h"
#include "guest_ops.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

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

static SDL_AudioStream *stream;
static SDL_Mutex *mix_lock;

enum { MIX_RATE = 22050, MIX_CHANNELS = 2 };

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
    SDL_UnlockMutex(mix_lock);
    com_ret(4, DD_OK);
}

static void sb_Stop(uint32_t self)
{
    SBuf *b = com_host(self);
    b->playing = 0;
    com_ret(1, DD_OK);
}

static void sb_GetCurrentPosition(uint32_t self)
{
    SBuf *b = com_host(self);
    if (ARG(1)) ST32(ARG(1), b->pos);
    if (ARG(2)) ST32(ARG(2), b->pos);
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
    const uint32_t bytes = LD32(desc + 12);      /* DSBUFFERDESC.dwBufferBytes */
    const uint32_t wfx = LD32(desc + 20);        /* .lpwfxFormat */

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
    if (nbufs < MAX_BUFS) bufs[nbufs++] = b;
    ST32(out, com_create(IF_DSBUFFER, b));
    com_ret(4, DD_OK);
}

static void ds_ret_ok2(uint32_t self) { (void)self; com_ret(2, DD_OK); }
static void ds_ret_ok3(uint32_t self) { (void)self; com_ret(3, DD_OK); }

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
    c->method[5] = ds_ret_ok3;                   /* DuplicateSoundBuffer */
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
