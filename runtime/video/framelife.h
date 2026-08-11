/* THE RENDERER'S FRAME LIFETIME, as arithmetic over plain integers.
 *
 * Everything here answers a question about WHEN a frame's display list is cleared, HOW an
 * overlay drawn over a frozen frame is rewound, and WHICH pooled texture serves a tile. None
 * of that involves a window, a GPU or the game, so none of it is verified by running one --
 * runtime/video/render.c includes this header and calls into it, and tests/test_framelife.c
 * walks it in microseconds (issues #52, #53).
 *
 * This is the same arrangement as runtime/overrides/geom.h: the shipping code INCLUDES the
 * header rather than keeping its own copy, so the test is not exercising a duplicate that can
 * drift. render.c owns the SDL objects; this owns the bookkeeping that decides what happens
 * to them, and the two are indexed alike.
 *
 * WHAT THE BOOKKEEPING IS FOR. The game's own frames are easy: record, draw, present, clear.
 * The pause menu is not. It freezes the world by declining to call the game's update, so on a
 * paused frame the game records NOTHING -- and a renderer that cleared its list at the last
 * present would have nothing to draw the menu on top of. So:
 *
 *   - a frame is marked SPENT at the present and cleared by the first call that RECORDS over
 *     it, which on an ordinary frame is the same thing one instruction later
 *   - a HELD frame is the retained one, extended with the port's own UI and rewound before the
 *     next extension, so a menu up for a minute is recorded once per frame rather than
 *     appended to itself 3600 times
 *   - the overlay boundary says where the game's picture ends, because the port's UI must be
 *     drawn after the lighting rather than lit with the scene
 */
#ifndef LF2_FRAMELIFE_H
#define LF2_FRAMELIFE_H

enum { FL_LISTS_MAX = 8, FL_POOL_MAX = 512 };

typedef struct {
    /* ---- the frame being built ---- */
    int      nlists;
    int      n[FL_LISTS_MAX];             /* entries recorded per destination surface */
    int      overlay_from[FL_LISTS_MAX];  /* where the port's own UI starts, or -1 */
    unsigned tile_used;                   /* bytes of the tile arena handed out */

    /* ---- the pooled tile textures ---- */
    int           pool_n;
    int           pool_w[FL_POOL_MAX], pool_h[FL_POOL_MAX];
    unsigned char pool_busy[FL_POOL_MAX];

    /* ---- the retained frame ---- */
    int           hold_n[FL_LISTS_MAX];
    unsigned      hold_tile_used;
    unsigned char hold_busy[FL_POOL_MAX];
    int           spent;                  /* presented; clear on the next recording call */
    int           holding;                /* this frame is the retained one, extended */

    /* ---- what the report prints ---- */
    long held_frames;
    int  peak_tiles, tiles_this_frame;
    long pool_exhausted;
} FrameLife;

/* Start of day, and after a shutdown. */
static inline void fl_init(FrameLife *f)
{
    int i;
    f->nlists = 0;
    f->tile_used = 0;
    f->pool_n = 0;
    f->hold_tile_used = 0;
    f->spent = 0;
    f->holding = 0;
    f->held_frames = 0;
    f->peak_tiles = 0;
    f->tiles_this_frame = 0;
    f->pool_exhausted = 0;
    for (i = 0; i < FL_LISTS_MAX; i++) { f->n[i] = 0; f->overlay_from[i] = -1; f->hold_n[i] = 0; }
    for (i = 0; i < FL_POOL_MAX; i++) { f->pool_busy[i] = 0; f->hold_busy[i] = 0; }
}

/* A new destination surface. Returns its index, or -1 when there is no room. render.c keeps
 * its Entry array at the same index. */
static inline int fl_list_add(FrameLife *f)
{
    if (f->nlists >= FL_LISTS_MAX) return -1;
    const int i = f->nlists++;
    f->n[i] = 0;
    f->overlay_from[i] = -1;
    return i;
}

/* Empty every list and release the whole pool. Not called directly by the frame loop -- see
 * fl_touch, which is what decides the moment. */
static inline void fl_clear(FrameLife *f)
{
    int i;
    for (i = 0; i < f->nlists; i++) { f->n[i] = 0; f->overlay_from[i] = -1; }
    for (i = 0; i < f->pool_n; i++) f->pool_busy[i] = 0;
    f->tile_used = 0;
    f->tiles_this_frame = 0;
}

/* Called by every entry point that RECORDS. Returns 1 if it cleared a spent frame, which is
 * what render.c needs in order to drop the SDL texture pointers that went with it. */
static inline int fl_touch(FrameLife *f)
{
    if (!f->spent) return 0;
    f->spent = 0;
    fl_clear(f);
    return 1;
}

/* Called after every present. Marks the frame spent and, unless this frame WAS the retained
 * one, records its extent so an overlay over it can be rewound.
 *
 * The `holding` guard is the whole reason this is not one line: on a held frame the list ends
 * with the pause menu, and recording THAT as the frame's extent is how the menu would become
 * part of the frozen picture and be drawn again, on top of itself, every frame after. */
static inline void fl_frame_reset(FrameLife *f)
{
    if (!f->holding) {
        int i;
        for (i = 0; i < f->nlists; i++) f->hold_n[i] = f->n[i];
        f->hold_tile_used = f->tile_used;
        for (i = 0; i < f->pool_n; i++) f->hold_busy[i] = f->pool_busy[i];
    }
    f->holding = 0;
    f->spent = 1;
}

/* Extend the retained frame instead of building a new one. Returns 0 when there is no
 * retained frame -- nothing has been drawn yet -- and the caller must then do without.
 *
 * Rewinding is what keeps a held frame bounded: the previous hold's overlay is dropped, and
 * the tile arena and the texture pool go back with it, so the pool does not leak a texture per
 * frame while the game sits paused. Entries from hold_n[i] up are the ones render.c must
 * forget the textures of; the caller reads the returned boundary from f->hold_n. */
static inline int fl_hold_begin(FrameLife *f)
{
    int i, any = 0;
    for (i = 0; i < f->nlists; i++) if (f->hold_n[i] > 0) any = 1;
    if (!any) return 0;
    for (i = 0; i < f->nlists; i++) {
        f->n[i] = f->hold_n[i];
        /* Everything recorded from here is the port's UI over a frozen picture. */
        f->overlay_from[i] = f->hold_n[i];
    }
    f->tile_used = f->hold_tile_used;
    for (i = 0; i < f->pool_n; i++) f->pool_busy[i] = f->hold_busy[i];
    f->spent = 0;
    f->holding = 1;
    f->tiles_this_frame = 0;
    f->held_frames++;
    return 1;
}

/* The port is about to draw its own UI over a LIVE frame -- the controls hint on a menu the
 * game is still updating. Same boundary fl_hold_begin sets for a frozen one, and it does not
 * move once set: the hint marks itself on every glyph and only the first is the boundary. */
static inline void fl_overlay_mark(FrameLife *f, int list)
{
    if (list < 0 || list >= f->nlists) return;
    if (f->overlay_from[list] < 0) f->overlay_from[list] = f->n[list];
}

/* Where the game's picture ends in a list. Always a valid index into [0, n]. */
static inline int fl_overlay_at(const FrameLife *f, int list)
{
    if (list < 0 || list >= f->nlists) return 0;
    const int ov = f->overlay_from[list];
    return ov >= 0 && ov <= f->n[list] ? ov : f->n[list];
}

/* ---- the tile texture pool ----
 *
 * A POOLED TEXTURE MAY BE BIGGER THAN THE TILE IT SERVES, and the tile uses its top-left
 * corner. Keying on the EXACT size instead rests on "a tile's size repeats constantly", which
 * is true of a glyph -- one 8x16 cell scaled -- and FALSE of the other caller: h_TextOutA
 * rasterises a whole string as one tile, so its width is that string's width and a run meets
 * as many sizes as it meets distinct strings.
 *
 * THE POOL MUST BE AT LEAST AS LARGE AS THE BUSIEST FRAME, not as the number of distinct
 * sizes: every tile in a frame is live at the same moment, uploaded and then drawn with the
 * draw free to be deferred, so no two tiles can share a texture within a frame.
 */
static inline int fl_pool_bucket(int v) { return (v + 31) & ~31; }

/* Claim the smallest free texture that fits, so a 16x32 glyph does not take the one texture a
 * 900-pixel string needs. Returns its index, or -1 if a new one must be created. */
static inline int fl_pool_claim(FrameLife *f, int tw, int th)
{
    int i, best = -1;
    for (i = 0; i < f->pool_n; i++) {
        if (f->pool_busy[i] || f->pool_w[i] < tw || f->pool_h[i] < th) continue;
        if (best < 0 || (long)f->pool_w[i] * f->pool_h[i]
                      < (long)f->pool_w[best] * f->pool_h[best])
            best = i;
    }
    if (best >= 0) f->pool_busy[best] = 1;
    return best;
}

/* Add a texture of the bucketed size for a tile that nothing free could serve, claimed. -1
 * when the pool is full, which the caller must report as text MISSING from the frame. */
static inline int fl_pool_add(FrameLife *f, int tw, int th)
{
    if (f->pool_n >= FL_POOL_MAX) { f->pool_exhausted++; return -1; }
    const int i = f->pool_n++;
    f->pool_w[i] = fl_pool_bucket(tw);
    f->pool_h[i] = fl_pool_bucket(th);
    f->pool_busy[i] = 1;
    return i;
}

/* One tile drawn, for the high-water mark the report prints beside the pool's size. */
static inline void fl_tile_drawn(FrameLife *f)
{
    if (++f->tiles_this_frame > f->peak_tiles) f->peak_tiles = f->tiles_this_frame;
}

#endif
