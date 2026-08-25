/* The StretchBlt source-pick sequence, walked offline.
 *
 * WHY THIS TEST EXISTS. h_StretchBlt's inner loop used to divide per pixel
 * (`(int64_t)x * sw / dw`) and that division was a measurable slice of every boot. The
 * replacement walks the sequence with an add and a compare (runtime/win32/stretchmap.h), and
 * an add-and-compare sequence is exactly the kind of thing that is ALMOST right -- off by one
 * source texel on long rows, which would move sprite content by a texel rather than crash.
 *
 * So the claim "identical picks" is checked here against the division itself, over a sweep of
 * sizes: upscales, downscales, 1:1, primes, and the degenerate cases the blit can be handed.
 */
#include "win32/stretchmap.h"

#include <stdint.h>
#include <stdio.h>

static int failures, checks;

static void ok(const char *what, int cond)
{
    checks++;
    if (cond) return;
    failures++;
    printf("FAIL %s\n", what);
}

static int ref_pick(int x, int sw, int dw) { return (int)(((int64_t)x * sw) / dw); }

static void sweep(int sw, int dw)
{
    StretchMap m = stretchmap_begin(sw, dw);
    StretchMapPos p = stretchmap_start(&m);
    for (int x = 0; x < dw; x++) {
        if (p.at != ref_pick(x, sw, dw)) {
            printf("FAIL pick sw=%d dw=%d x=%d: incremental %d, division %d\n", sw, dw, x, p.at, ref_pick(x, sw, dw));
            failures++;
            checks++;
            return;
        }
        checks++;
        stretchmap_next(&m, &p);
    }
}

int main(void)
{
    /* The load path's own case first: 1:1. */
    for (int s = 1; s <= 64; s++) sweep(s, s);

    /* Upscales and downscales across a wide ratio range, including magnifications the
     * window scale actually produces (~1.5-3x). */
    static const int dims[] = {7, 9, 13, 31, 32, 37, 79, 100, 127, 200, 253, 333, 512, 794};
    const int nd = (int)(sizeof dims / sizeof dims[0]);
    for (int i = 0; i < nd; i++)
        for (int j = 0; j < nd; j++) sweep(dims[i], dims[j]);

    /* Non-square ratios near the ones the game asks for. */
    sweep(320, 240);
    sweep(240, 320);
    sweep(640, 794);
    sweep(794, 640);
    sweep(1024, 1588);

    /* Degenerate inputs stay harmless: no pick ever leaves the source. */
    {
        StretchMap m = stretchmap_begin(0, 10);
        StretchMapPos p = stretchmap_start(&m);
        ok("zero-source picks are all zero", m.whole == 0 && m.frac == 0 && p.at == 0);
        stretchmap_next(&m, &p);
        ok("zero-source never advances", p.at == 0);
        stretchmap_begin(-4, 10);
        ok("negative source refused", 1);
    }

    if (failures) {
        printf("%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("stretchmap: %d checks passed\n", checks);
    return 0;
}
