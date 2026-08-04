#include "mixer.h"

/* One sample of frame `i`, channel `ch`, on the int16 scale. 8-bit PCM is unsigned with
 * 128 as silence, which is why it is biased before scaling. */
static float samp_at(const MixSrc *s, uint32_t i, int ch)
{
    const int sc = (ch < s->channels) ? ch : 0;
    const uint32_t off = i * (uint32_t)((s->bits / 8) * s->channels);
    if (s->bits == 8)
        return (float)(((int32_t)s->pcm[off + (uint32_t)sc] - 128) << 8);
    const uint8_t *p = s->pcm + off + (uint32_t)(sc * 2);
    return (float)(int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int mixer_add(int16_t *out, int frames, int out_channels, int out_rate,
              const MixSrc *src, double *cursor, int looping, float gain)
{
    if (!src || !src->pcm || src->frames == 0 || out_rate <= 0) return 0;
    if (src->bits != 8 && src->bits != 16) return 0;
    if (src->channels <= 0 || src->rate <= 0) return 0;

    const double step = (double)src->rate / (double)out_rate;

    for (int f = 0; f < frames; f++) {
        if (*cursor < 0) *cursor = 0;
        uint32_t i0 = (uint32_t)*cursor;

        /* Interpolation needs i0 and i0+1. Running out of the pair is the end of the
         * source, not merely the end of the array. */
        if (i0 + 1 >= src->frames) {
            if (!looping) return 0;
            /* Wrap by the source length so the fraction is preserved across the seam. */
            *cursor -= (double)src->frames;
            if (*cursor < 0) *cursor = 0;
            i0 = (uint32_t)*cursor;
            if (i0 + 1 >= src->frames) return 0;   /* shorter than one pair */
        }

        const float frac = (float)(*cursor - (double)i0);
        for (int c = 0; c < out_channels; c++) {
            const float v = samp_at(src, i0, c) * (1.0f - frac)
                          + samp_at(src, i0 + 1, c) * frac;
            int32_t acc = out[f * out_channels + c] + (int32_t)(v * gain);
            out[f * out_channels + c] = (int16_t)(acc > 32767 ? 32767 :
                                                  acc < -32768 ? -32768 : acc);
        }
        *cursor += step;
    }
    return 1;
}
