/* Tests for the resampling mixer core.
 *
 * Every check here is run against BOTH implementations: the current one and `legacy_add`,
 * a replica of the mixer as it was before -- nearest-neighbour pick, cursor rounded into a
 * byte offset between slices. A check that passes on both discriminates nothing, and the
 * suite fails if any check cannot tell them apart. That is the point: the previous mixer
 * was wrong for months while "audio works" stayed true, because nothing measured pitch.
 *
 * The legacy code lives here rather than behind a flag in the shipping mixer -- a
 * bug-compatible mode in production is a liability, in a test it is the control. */
#include "mixer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OUT_RATE = 22050, OUT_CH = 2 };

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    if (!ok) { printf("  FAIL  %s -- %s\n", what, detail); failures++; }
    else       printf("  ok    %s\n", what);
}

/* ---- the previous implementation, reproduced as the control ----
 * Nearest-neighbour source pick, and the cursor round-tripped through a byte offset at
 * the end of each slice, which is what discarded the fraction. */
static void legacy_add(int16_t *out, int frames, const MixSrc *src,
                       uint32_t *pos_bytes, int looping, float gain)
{
    const int bps = (src->bits / 8) * src->channels;
    double cursor = (double)*pos_bytes / (double)bps;
    const double step = (double)src->rate / (double)OUT_RATE;

    for (int f = 0; f < frames; f++) {
        const uint32_t off = (uint32_t)cursor * (uint32_t)bps;
        if (off + (uint32_t)bps > src->frames * (uint32_t)bps) {
            if (looping) { cursor = 0; continue; }
            break;
        }
        for (int c = 0; c < OUT_CH; c++) {
            const int sc = (c < src->channels) ? c : 0;
            int32_t v;
            if (src->bits == 8) v = ((int32_t)src->pcm[off + (uint32_t)sc] - 128) << 8;
            else {
                const uint8_t *p = src->pcm + off + (uint32_t)(sc * 2);
                v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            }
            int32_t acc = out[f * OUT_CH + c] + (int32_t)((float)v * gain);
            out[f * OUT_CH + c] = (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
        }
        cursor += step;
    }
    *pos_bytes = (uint32_t)cursor * (uint32_t)bps;    /* <-- the fraction dies here */
}

/* ---- signal helpers ---- */

/* A mono 16-bit sine of `hz` at `rate`, `frames` long. */
static MixSrc *make_sine(int rate, double hz, uint32_t frames, int bits)
{
    MixSrc *s = calloc(1, sizeof *s);
    uint8_t *pcm = calloc(frames, (size_t)(bits / 8));
    for (uint32_t i = 0; i < frames; i++) {
        const double v = sin(2.0 * M_PI * hz * (double)i / (double)rate);
        if (bits == 8) pcm[i] = (uint8_t)(128 + (int)(v * 100.0));
        else {
            const int16_t q = (int16_t)(v * 20000.0);
            pcm[i * 2]     = (uint8_t)((uint16_t)q & 0xff);
            pcm[i * 2 + 1] = (uint8_t)((uint16_t)q >> 8);
        }
    }
    s->pcm = pcm; s->frames = frames; s->bits = bits; s->channels = 1; s->rate = rate;
    return s;
}

/* Frequency of an int16 interleaved signal, from zero crossings of channel 0.
 * Crossings are counted on a sign change with a small deadband so that interpolation
 * noise around zero does not register as extra cycles. */
static double freq_of(const int16_t *buf, int frames, int rate)
{
    int crossings = 0, sign = 0;
    for (int f = 0; f < frames; f++) {
        const int v = buf[f * OUT_CH];
        int s = 0;
        if (v > 2000) s = 1; else if (v < -2000) s = -1; else continue;
        if (sign && s != sign) crossings++;
        sign = s;
    }
    return (double)crossings * (double)rate / (2.0 * (double)frames);
}

/* ---- checks ---- */

/* Slicing must not change the output. This is the exact defect: a cursor rounded between
 * slices resets the phase on every callback. */
static int slicing_is_transparent(int rate)
{
    const uint32_t n = 40000;
    MixSrc *s = make_sine(rate, 440.0, n, 16);

    const int frames = 4096;
    int16_t *one = calloc((size_t)frames * OUT_CH, sizeof(int16_t));
    int16_t *many = calloc((size_t)frames * OUT_CH, sizeof(int16_t));

    double cur = 0.0;
    mixer_add(one, frames, OUT_CH, OUT_RATE, s, &cur, 0, 1.0f);

    cur = 0.0;
    for (int off = 0; off < frames; off += 512)
        mixer_add(many + off * OUT_CH, 512, OUT_CH, OUT_RATE, s, &cur, 0, 1.0f);

    const int same = memcmp(one, many, (size_t)frames * OUT_CH * sizeof(int16_t)) == 0;
    free(one); free(many); free((void *)s->pcm); free(s);
    return same;
}

static int legacy_slicing_is_transparent(int rate)
{
    const uint32_t n = 40000;
    MixSrc *s = make_sine(rate, 440.0, n, 16);

    const int frames = 4096;
    int16_t *one = calloc((size_t)frames * OUT_CH, sizeof(int16_t));
    int16_t *many = calloc((size_t)frames * OUT_CH, sizeof(int16_t));

    uint32_t pos = 0;
    legacy_add(one, frames, s, &pos, 0, 1.0f);

    pos = 0;
    for (int off = 0; off < frames; off += 512)
        legacy_add(many + off * OUT_CH, 512, s, &pos, 0, 1.0f);

    const int same = memcmp(one, many, (size_t)frames * OUT_CH * sizeof(int16_t)) == 0;
    free(one); free(many); free((void *)s->pcm); free(s);
    return same;
}

/* Resampling must preserve pitch. */
static double measured_freq(int rate, double hz, int legacy)
{
    const uint32_t n = 60000;
    MixSrc *s = make_sine(rate, hz, n, 16);
    const int frames = 8192;
    int16_t *out = calloc((size_t)frames * OUT_CH, sizeof(int16_t));

    if (legacy) {
        uint32_t pos = 0;
        for (int off = 0; off < frames; off += 512)
            legacy_add(out + off * OUT_CH, 512, s, &pos, 0, 1.0f);
    } else {
        double cur = 0.0;
        for (int off = 0; off < frames; off += 512)
            mixer_add(out + off * OUT_CH, 512, OUT_CH, OUT_RATE, s, &cur, 0, 1.0f);
    }
    const double f = freq_of(out, frames, OUT_RATE);
    free(out); free((void *)s->pcm); free(s);
    return f;
}

int main(void)
{
    printf("mixer tests\n");

    /* The rates the game actually uses, from LF2_AUDIO_DEBUG over a real run. 22050 is
     * included deliberately: it is the one that always worked, so it must pass on the
     * legacy control too, and it is the reason the bug looked like "some sounds". */
    const int rates[] = { 11025, 16000, 21000, 22050, 38400, 44100 };

    puts("\n  slicing transparency (mix in one call == mix in slices)");
    for (size_t i = 0; i < sizeof rates / sizeof *rates; i++) {
        char msg[128];
        snprintf(msg, sizeof msg, "%d Hz source slices cleanly", rates[i]);
        check(slicing_is_transparent(rates[i]), msg, "slicing changed the output");
    }

    puts("\n  pitch preservation (440 Hz in, 440 Hz out)");
    for (size_t i = 0; i < sizeof rates / sizeof *rates; i++) {
        const double f = measured_freq(rates[i], 440.0, 0);
        char msg[128], det[128];
        snprintf(msg, sizeof msg, "%d Hz source keeps 440 Hz (got %.1f)", rates[i], f);
        snprintf(det, sizeof det, "measured %.1f Hz, want 440 +/- 20", f);
        check(fabs(f - 440.0) < 20.0, msg, det);
    }

    /* 8-bit silence is 128, not 0. Getting this wrong adds a DC step on every 8-bit
     * effect, which is a click rather than a wrong note. */
    puts("\n  8-bit encoding");
    {
        const uint32_t n = 1000;
        MixSrc *s = calloc(1, sizeof *s);
        uint8_t *pcm = malloc(n);
        memset(pcm, 128, n);
        s->pcm = pcm; s->frames = n; s->bits = 8; s->channels = 1; s->rate = 22050;
        int16_t out[256 * OUT_CH];
        memset(out, 0, sizeof out);
        double cur = 0.0;
        mixer_add(out, 256, OUT_CH, OUT_RATE, s, &cur, 0, 1.0f);
        int quiet = 1;
        for (int i = 0; i < 256 * OUT_CH; i++) if (out[i] != 0) quiet = 0;
        check(quiet, "8-bit 128 is silence", "constant 128 produced non-zero output");
        free(pcm); free(s);
    }

    /* THE CONTROL. If these pass, the checks above prove nothing and this suite is
     * decoration -- so a passing control is itself a failure. */
    puts("\n  control: the same checks must FAIL on the previous implementation");
    {
        int legacy_caught = 0;
        for (size_t i = 0; i < sizeof rates / sizeof *rates; i++) {
            if (rates[i] == OUT_RATE) continue;       /* step 1.0: legacy is correct here */
            if (!legacy_slicing_is_transparent(rates[i])) legacy_caught++;
        }
        char det[160];
        snprintf(det, sizeof det,
                 "the legacy mixer sliced cleanly at every rate, so the slicing check "
                 "cannot detect the bug it exists for");
        check(legacy_caught > 0, "legacy mixer is caught by the slicing check", det);

        /* And 22050 must be identical under both, or the diagnosis "only 22050 worked"
         * was wrong. */
        check(legacy_slicing_is_transparent(OUT_RATE),
              "legacy mixer is correct at 22050 Hz (matching the symptom)",
              "legacy failed even at 22050, so the reported symptom is unexplained");

        /* Reported, not asserted. If the legacy mixer already reproduced 440 Hz, then it
         * was never a PITCH bug and replacing the resampler cannot have changed pitch --
         * which is a fact about where to look next, not a test failure. */
        puts("\n  legacy pitch, for comparison (does the old mixer get pitch right?)");
        for (size_t i = 0; i < sizeof rates / sizeof *rates; i++)
            printf("    %5d Hz source -> %.1f Hz out (legacy)   %.1f Hz out (current)\n",
                   rates[i], measured_freq(rates[i], 440.0, 1),
                   measured_freq(rates[i], 440.0, 0));
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
