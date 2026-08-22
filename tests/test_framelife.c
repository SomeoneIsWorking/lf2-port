/* The renderer's display-list lifetime, walked without a window (issues #52, #53, #94).
 *
 * runtime/video/render.c INCLUDES runtime/video/framelife.h and calls into it, so this is not
 * exercising a copy -- same arrangement as tests/test_geom.c over runtime/overrides/geom.h.
 *
 * RmlUi has no separate retained/frozen frame; each game update uses this ordinary lifetime.
 */
#include "framelife.h"

#include <stdio.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static void check_eq(long got, long want, const char *what)
{
    if (got != want) {
        printf("  FAIL  %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}

/* One ordinary frame: record, present, and clear immediately. */
static void ordinary_frames(void)
{
    FrameLife f;
    fl_init(&f);
    const int L = fl_list_add(&f);
    check_eq(L, 0, "the first list is index 0");

    f.n[L] = 40;
    f.tile_used = 4096;
    fl_frame_reset(&f);
    check_eq(f.n[L], 0, "present consumes the completed display list");
    check_eq(f.tile_used, 0, "present rewinds the tile arena");

    f.n[L] = 12;
    fl_frame_reset(&f);
    check_eq(f.n[L], 0, "every present has the same immediate-clear boundary");
}

/* The overlay boundary separates what the lighting touches from what it must not. */
static void overlay_boundary(void)
{
    FrameLife f;
    fl_init(&f);
    const int L = fl_list_add(&f);

    check_eq(fl_overlay_at(&f, L), 0, "an empty list has nothing before the overlay");
    f.n[L] = 50;
    check_eq(fl_overlay_at(&f, L), 50, "with no mark, the whole list is the game's picture");

    fl_overlay_mark(&f, L);
    check_eq(fl_overlay_at(&f, L), 50, "the mark is where the port's UI starts");
    f.n[L] = 53;
    fl_overlay_mark(&f, L);
    check_eq(fl_overlay_at(&f, L), 50, "and it does NOT move -- the hint marks on every glyph");

    check_eq(fl_overlay_at(&f, 7), 0, "a list that does not exist reports no picture");
}

/* The pool. The bug this replaces: keyed on EXACT size, it grew one texture per distinct
 * string width until it hit its cap and dropped tiles. */
static void pool_reuse(void)
{
    FrameLife f;
    fl_init(&f);

    check_eq(fl_pool_claim(&f, 16, 32), -1, "an empty pool can serve nothing");
    const int a = fl_pool_add(&f, 16, 32);
    check_eq(a, 0, "the first texture is index 0");
    check_eq(f.pool_w[a], 32, "the width is bucketed up to 32");
    check_eq(f.pool_h[a], 32, "and so is the height");
    check_eq(f.pool_busy[a], 1, "a texture added for a tile is claimed by it");

    /* Two tiles in the SAME frame cannot share a texture: both are live at once. */
    check_eq(fl_pool_claim(&f, 16, 32), -1, "a busy texture is not offered to a second tile");
    const int b = fl_pool_add(&f, 16, 32);
    check_eq(b, 1, "so the second tile gets its own");

    /* A different string width must NOT need a new texture if a big enough one is free. */
    fl_clear(&f);
    check_eq(f.pool_busy[a], 0, "the frame reset releases the pool");
    const int wide = fl_pool_add(&f, 900, 20);
    check_eq(f.pool_w[wide], 928, "928, the 32-grid bucket above 900");
    fl_clear(&f);
    check_eq(fl_pool_claim(&f, 880, 18), wide,
             "a DIFFERENT string width reuses it -- this is the fix for the exhaustion");
    check_eq(fl_pool_claim(&f, 900, 20), -1, "and only one tile at a time may hold it");

    /* The smallest that fits, so a glyph does not take the wide one. */
    fl_clear(&f);
    check_eq(fl_pool_claim(&f, 8, 8), a, "a small tile takes a small texture, not the wide one");
    check_eq(fl_pool_claim(&f, 8, 8), b, "and then the other small one");
    check_eq(fl_pool_claim(&f, 8, 8), wide, "only when the small ones are gone");

    /* Exhaustion is COUNTED, because a dropped tile is text missing from the picture. */
    FrameLife g;
    fl_init(&g);
    for (int i = 0; i < FL_POOL_MAX; i++) check(fl_pool_add(&g, 8, 8) == i, "the pool fills");
    check_eq(fl_pool_add(&g, 8, 8), -1, "and then refuses");
    check_eq(g.pool_exhausted, 1, "and says so, once per refusal");
}

/* The high-water mark the report prints beside the pool's size: it must be a PER-FRAME peak,
 * not a running total, or it says nothing about whether the pool is big enough. */
static void tile_peak(void)
{
    FrameLife f;
    fl_init(&f);
    for (int i = 0; i < 40; i++) fl_tile_drawn(&f);
    fl_clear(&f);
    for (int i = 0; i < 121; i++) fl_tile_drawn(&f);
    fl_clear(&f);
    for (int i = 0; i < 30; i++) fl_tile_drawn(&f);
    check_eq(f.peak_tiles, 121, "the peak is the busiest single frame");
}

int main(void)
{
    ordinary_frames();
    overlay_boundary();
    pool_reuse();
    tile_peak();

    if (failures) {
        printf("frame lifetime: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("frame lifetime: ok\n");
    return 0;
}
