/* Blit equivalence tests.
 *
 * The blit inner loop was doing a 64-bit multiply and an integer DIVIDE per pixel, which
 * stack sampling showed dominating the loading screen. Hoisting that to one divide per
 * column is a pure optimisation, so the bar is not "looks right" but "produces exactly
 * the same pixels" -- this path draws every sprite in the game, and a one-pixel shift in a
 * scaled blit is the kind of thing that is invisible in a screenshot and wrong forever.
 *
 * Comparing frame dumps across two runs cannot decide this: the game's load timing varies,
 * so frame N holds different content run to run (135,895 pixels "differed" that way). The
 * reference implementation is therefore carried here, exactly as it was, and both are run
 * over the same synthetic surfaces.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t *px; int w, h, pitch_px; } Surf;

static int failures, checks;

/* ---- the previous implementation, verbatim in shape, as the reference ---- */
static void blit_ref(Surf *d, int dx, int dy, int dw, int dh,
                     Surf *s, int sx, int sy, int sw, int sh,
                     int keyed, uint32_t klo, uint32_t khi)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dh; y++) {
        const int syy = sy + (int)((int64_t)y * sh / dh), dyy = dy + y;
        if (syy < 0 || syy >= s->h || dyy < 0 || dyy >= d->h) continue;
        const uint32_t *sp = s->px + (size_t)syy * (size_t)s->pitch_px;
        uint32_t *dp = d->px + (size_t)dyy * (size_t)d->pitch_px;
        for (int x = 0; x < dw; x++) {
            const int sxx = sx + (int)((int64_t)x * sw / dw), dxx = dx + x;
            if (sxx < 0 || sxx >= s->w || dxx < 0 || dxx >= d->w) continue;
            const uint32_t v = sp[sxx] & 0x00ffffffu;
            if (keyed && v >= (klo & 0x00ffffffu) && v <= (khi & 0x00ffffffu)) continue;
            dp[dxx] = v;
        }
    }
}

/* ---- the optimised implementation, same shape as runtime/video/ddraw.c ---- */
enum { BLIT_MAXW = 4096 };

static void blit_fast(Surf *d, int dx, int dy, int dw, int dh,
                      Surf *s, int sx, int sy, int sw, int sh,
                      int keyed, uint32_t klo, uint32_t khi)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    const uint32_t lo = klo & 0x00ffffffu, hi = khi & 0x00ffffffu;

    static int col_src[BLIT_MAXW], col_dst[BLIT_MAXW];
    int ncol = 0;
    const int wlim = dw < BLIT_MAXW ? dw : BLIT_MAXW;
    for (int x = 0; x < wlim; x++) {
        const int sxx = sx + (int)((int64_t)x * sw / dw), dxx = dx + x;
        if (sxx < 0 || sxx >= s->w || dxx < 0 || dxx >= d->w) continue;
        col_src[ncol] = sxx; col_dst[ncol] = dxx; ncol++;
    }
    if (!ncol) return;

    const int direct = (ncol > 1) && (col_src[1] - col_src[0] == 1)
                    && (col_src[ncol - 1] - col_src[0] == ncol - 1)
                    && (col_dst[ncol - 1] - col_dst[0] == ncol - 1);

    for (int y = 0; y < dh; y++) {
        const int syy = sy + (int)((int64_t)y * sh / dh), dyy = dy + y;
        if (syy < 0 || syy >= s->h || dyy < 0 || dyy >= d->h) continue;
        const uint32_t *sp = s->px + (size_t)syy * (size_t)s->pitch_px;
        uint32_t *dp = d->px + (size_t)dyy * (size_t)d->pitch_px;

        if (direct) {
            const uint32_t *srow = sp + col_src[0];
            uint32_t *drow = dp + col_dst[0];
            if (!keyed) {
                for (int i = 0; i < ncol; i++) drow[i] = srow[i] & 0x00ffffffu;
            } else {
                for (int i = 0; i < ncol; i++) {
                    const uint32_t v = srow[i] & 0x00ffffffu;
                    if (v >= lo && v <= hi) continue;
                    drow[i] = v;
                }
            }
        } else if (!keyed) {
            for (int i = 0; i < ncol; i++) dp[col_dst[i]] = sp[col_src[i]] & 0x00ffffffu;
        } else {
            for (int i = 0; i < ncol; i++) {
                const uint32_t v = sp[col_src[i]] & 0x00ffffffu;
                if (v >= lo && v <= hi) continue;
                dp[col_dst[i]] = v;
            }
        }
    }
}

/* ---- harness ---- */

static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static Surf *make(int w, int h, int fill_random)
{
    Surf *s = calloc(1, sizeof *s);
    s->w = w; s->h = h; s->pitch_px = w + 3;          /* a pitch wider than w, as real ones are */
    s->px = calloc((size_t)s->pitch_px * (size_t)h, sizeof(uint32_t));
    if (fill_random)
        for (int i = 0; i < s->pitch_px * h; i++) s->px[i] = rnd();
    return s;
}

static void one_case(const char *what, int dx, int dy, int dw, int dh,
                     int sx, int sy, int sw, int sh, int keyed, uint32_t lo, uint32_t hi)
{
    Surf *src = make(97, 61, 1);
    Surf *a = make(120, 80, 1), *b = make(120, 80, 0);
    memcpy(b->px, a->px, (size_t)a->pitch_px * (size_t)a->h * sizeof(uint32_t));

    blit_ref (a, dx, dy, dw, dh, src, sx, sy, sw, sh, keyed, lo, hi);
    blit_fast(b, dx, dy, dw, dh, src, sx, sy, sw, sh, keyed, lo, hi);

    checks++;
    const size_t n = (size_t)a->pitch_px * (size_t)a->h;
    if (memcmp(a->px, b->px, n * sizeof(uint32_t)) != 0) {
        size_t first = 0;
        for (size_t i = 0; i < n; i++) if (a->px[i] != b->px[i]) { first = i; break; }
        printf("  FAIL  %s: dst(%d,%d %dx%d) src(%d,%d %dx%d) keyed=%d "
               "-- first difference at index %zu, ref %08x fast %08x\n",
               what, dx, dy, dw, dh, sx, sy, sw, sh, keyed,
               first, a->px[first], b->px[first]);
        failures++;
    }
    free(src->px); free(src); free(a->px); free(a); free(b->px); free(b);
}

int main(void)
{
    printf("blit equivalence tests\n");

    /* 1:1, the common case and the one the fast path takes. */
    one_case("unscaled", 0, 0, 90, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("unscaled keyed", 0, 0, 90, 55, 0, 0, 90, 55, 1, 0x000000, 0x00ffff);
    one_case("unscaled offset", 12, 7, 60, 40, 5, 9, 60, 40, 0, 0, 0);

    /* Scaling in both directions, where the per-column table has to agree exactly with
     * the per-pixel divide it replaced. */
    one_case("scaled up",   0, 0, 100, 70, 0, 0, 50, 35, 0, 0, 0);
    one_case("scaled down", 0, 0,  40, 25, 0, 0, 90, 55, 0, 0, 0);
    one_case("scaled x only", 0, 0, 80, 55, 0, 0, 40, 55, 0, 0, 0);
    one_case("scaled keyed", 0, 0, 100, 70, 0, 0, 50, 35, 1, 0x000000, 0x00ff00);
    one_case("non-integer scale", 0, 0, 77, 43, 0, 0, 31, 29, 0, 0, 0);

    /* Clipping: off every edge, which is where an off-by-one in the column table shows. */
    one_case("clip left",   -20, 0, 90, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("clip right",  100, 0, 90, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("clip top",    0, -15, 90, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("clip bottom", 0,  70, 90, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("clip src x",  0, 0, 90, 55, 60, 0, 90, 55, 0, 0, 0);
    one_case("clip both",  -10, -10, 90, 55, -5, -5, 90, 55, 0, 0, 0);
    one_case("fully off",  500, 500, 90, 55, 0, 0, 90, 55, 0, 0, 0);

    /* Degenerate sizes must be no-ops in both. */
    one_case("zero width",  0, 0, 0, 55, 0, 0, 90, 55, 0, 0, 0);
    one_case("negative dh", 0, 0, 90, -3, 0, 0, 90, 55, 0, 0, 0);

    /* A sweep, so the cases above are not the only shapes ever tried. */
    for (int i = 0; i < 400; i++) {
        const int dw = 1 + (int)(rnd() % 110), dh = 1 + (int)(rnd() % 75);
        const int sw = 1 + (int)(rnd() % 95),  sh = 1 + (int)(rnd() % 60);
        const int dx = (int)(rnd() % 160) - 30, dy = (int)(rnd() % 110) - 25;
        const int sx = (int)(rnd() % 120) - 20, sy = (int)(rnd() % 80) - 15;
        const int keyed = (int)(rnd() & 1);
        one_case("sweep", dx, dy, dw, dh, sx, sy, sw, sh, keyed, 0x000000, 0x00808080u);
    }

    printf("\n%d cases, %s (%d failures)\n", checks, failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
