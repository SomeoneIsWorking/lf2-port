/* The renderer's frame lifetime, walked without a window (issues #52, #53).
 *
 * runtime/video/render.c INCLUDES runtime/video/framelife.h and calls into it, so this is not
 * exercising a copy -- same arrangement as tests/test_geom.c over runtime/overrides/geom.h.
 *
 * WHAT MADE THIS WORTH WRITING. Every bug in the pause menu's first cut was in here, and each
 * one was found by looking at a 1920x1080 screenshot after a five-minute route run:
 *
 *   - a recording call cleared the retained frame out from under the overlay about to be drawn
 *     over it, and the failure was silent -- the list came back on rewind and only the TILE
 *     ARENA was gone, so the frozen frame's text was garbage while everything drawn from a
 *     cached texture looked perfect
 *   - the frame reset re-took the retained extent on a held frame, which folds the menu into
 *     the frozen picture so it is drawn on top of itself for as long as the pause lasts
 *   - the tile pool, keyed on exact size, filled and silently dropped 42402 tiles
 *
 * Not one of those needed a GPU to find. They needed these assertions.
 */
#include "framelife.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) { printf("  FAIL  %s\n", what); failures++; }
}

static void check_eq(long got, long want, const char *what)
{
    if (got != want) {
        printf("  FAIL  %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}

/* One ordinary frame: record, present, and the NEXT frame's first record is what clears. */
static void ordinary_frames(void)
{
    FrameLife f;
    fl_init(&f);
    const int L = fl_list_add(&f);
    check_eq(L, 0, "the first list is index 0");

    f.n[L] = 40;
    f.tile_used = 4096;
    fl_frame_reset(&f);
    check_eq(f.spent, 1, "after the present the frame is spent");
    check_eq(f.n[L], 40, "and NOT yet cleared -- clearing is the next record's job");
    check_eq(f.hold_n[L], 40, "the retained extent is the frame that was just presented");

    check_eq(fl_touch(&f), 1, "the first record of the next frame clears");
    check_eq(f.n[L], 0, "and the list is empty");
    check_eq(f.tile_used, 0, "and the tile arena is rewound");
    check_eq(fl_touch(&f), 0, "a second record in the same frame clears nothing");

    /* The whole point of the deferral: a frame that records NOTHING keeps the last one. */
    f.n[L] = 12;
    fl_frame_reset(&f);
    fl_frame_reset(&f);          /* two presents, no record between them */
    check_eq(f.n[L], 12, "a frame that recorded nothing still has the last frame's list");
}

/* The pause menu: hold, extend, rewind, and do it again. */
static void held_frames(void)
{
    FrameLife f;
    fl_init(&f);
    const int L = fl_list_add(&f);

    f.n[L] = 100;
    f.tile_used = 8192;
    fl_frame_reset(&f);                       /* the last frame the game built */

    for (int frame = 0; frame < 5; frame++) {
        check_eq(fl_hold_begin(&f), 1, "there is a retained frame to draw over");
        check_eq(f.n[L], 100, "the list is rewound to what the GAME built, every time");
        check_eq(f.tile_used, 8192, "and so is the tile arena");
        check_eq(fl_overlay_at(&f, L), 100, "the overlay boundary is where the game stopped");
        check_eq(f.spent, 0, "a held frame is not spent -- it is about to be presented again");

        /* The port draws its menu: a dim, a panel, four rows of text. */
        f.n[L] += 30;
        f.tile_used += 2048;
        check_eq(fl_touch(&f), 0, "recording the overlay does NOT clear the frame under it");

        fl_frame_reset(&f);
        check_eq(f.hold_n[L], 100, "the retained extent still EXCLUDES the overlay");
    }
    check_eq(f.n[L], 130, "one hold's worth of overlay, not five");
    check_eq(f.held_frames, 5, "and the count says five frames were held");

    /* Unpause: the game records again and everything goes back to normal. */
    check_eq(fl_touch(&f), 1, "the first real record after the pause clears");
    check_eq(f.n[L], 0, "the frozen frame is gone");
    check_eq(fl_overlay_at(&f, L), 0, "and so is the overlay boundary");
}

/* fl_hold_begin must refuse when there is nothing retained, because the caller has to keep
 * presenting the software frame in that case -- a GPU present with the menu missing leaves a
 * game that ignores every key and shows nothing to say why. */
static void nothing_to_hold(void)
{
    FrameLife f;
    fl_init(&f);
    fl_list_add(&f);
    check_eq(fl_hold_begin(&f), 0, "no retained frame yet, so the hold is refused");
    check_eq(f.held_frames, 0, "and nothing was counted as held");
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

    /* A held frame is a frame too -- its tiles must not be added to the one it froze. */
    FrameLife g;
    fl_init(&g);
    const int L = fl_list_add(&g);
    g.n[L] = 10;
    fl_frame_reset(&g);
    for (int i = 0; i < 5; i++) fl_tile_drawn(&g);
    fl_hold_begin(&g);
    for (int i = 0; i < 7; i++) fl_tile_drawn(&g);
    check_eq(g.peak_tiles, 7, "a hold starts the frame's tile count again");
}

int main(void)
{
    ordinary_frames();
    held_frames();
    nothing_to_hold();
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
