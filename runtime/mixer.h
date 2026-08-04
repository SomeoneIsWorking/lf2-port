/* The resampling mixer core, deliberately free of guest memory and SDL.
 *
 * This is the part that has to be provably correct -- a wrong step, a dropped fraction or
 * an off-by-one in the interpolation is inaudible in a smoke test that only asserts "sound
 * was produced", and that is exactly how a broken resampler survived. Taking a host
 * pointer instead of a guest address is what makes it testable at all. */
#ifndef LF2_MIXER_H
#define LF2_MIXER_H

#include <stdint.h>

typedef struct {
    const uint8_t *pcm;       /* interleaved PCM, host memory */
    uint32_t       frames;    /* number of frames available */
    int            bits;      /* 8 (unsigned, 128 = silence) or 16 (signed) */
    int            channels;
    int            rate;      /* Hz */
} MixSrc;

/* Adds `frames` output frames of `src` into `out` (interleaved int16 at `out_rate`),
 * resampled with linear interpolation.
 *
 * `cursor` is the fractional source-frame position and is updated in place. It is a
 * double, and it is the caller's, precisely so that mixing a signal in slices produces
 * bit-identical output to mixing it in one call: rounding it between slices resets the
 * phase every time and is audible.
 *
 * Returns 1 if the source is still playing, 0 once a non-looping source is exhausted. */
int mixer_add(int16_t *out, int frames, int out_channels, int out_rate,
              const MixSrc *src, double *cursor, int looping, float gain);

#endif
