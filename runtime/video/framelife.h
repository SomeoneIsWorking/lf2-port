/* THE RENDERER'S FRAME LIFETIME, as arithmetic over plain integers.
 *
 * Everything here answers a question about WHEN a frame's display list is cleared, WHERE the
 * port-owned overlay begins, and WHICH pooled texture serves a tile. None
 * of that involves a window, a GPU or the game, so none of it is verified by running one --
 * runtime/video/render.c includes this header and calls into it, and tests/test_framelife.c
 * walks it in microseconds (issues #52, #53).
 *
 * This is the same arrangement as runtime/overrides/geom.h: the shipping code INCLUDES the
 * header rather than keeping its own copy, so the test is not exercising a duplicate that can
 * drift. render.c owns the SDL objects; this owns the bookkeeping that decides what happens
 * to them, and the two are indexed alike.
 *
 * A display list has one ordinary lifetime: record, draw, present, clear. RmlUi does not
 * create a second frame lifecycle; the game continues through this same producer/present path
 * behind the modal. The overlay boundary only says where the game's picture ends, because the
 * port's UI must be drawn after the lighting rather than lit with the scene.
 */
#ifndef LF2_FRAMELIFE_H
#define LF2_FRAMELIFE_H

enum { FL_LISTS_MAX = 8, FL_POOL_MAX = 512 };

typedef struct {
    /* ---- the frame being built ---- */
    int nlists;
    int n[FL_LISTS_MAX];            /* entries recorded per destination surface */
    int overlay_from[FL_LISTS_MAX]; /* where the port's own UI starts, or -1 */
    unsigned tile_used;             /* bytes of the tile arena handed out */

    /* ---- the pooled tile textures ---- */
    int pool_n;
    int pool_w[FL_POOL_MAX], pool_h[FL_POOL_MAX];
    unsigned char pool_busy[FL_POOL_MAX];

    /* ---- what the report prints ---- */
    int peak_tiles, tiles_this_frame;
    long pool_exhausted;
} FrameLife;

/* Start of day, and after a shutdown. */
static inline void fl_init(FrameLife *f)
{
    int i;
    f->nlists = 0;
    f->tile_used = 0;
    f->pool_n = 0;
    f->peak_tiles = 0;
    f->tiles_this_frame = 0;
    f->pool_exhausted = 0;
    for (i = 0; i < FL_LISTS_MAX; i++) {
        f->n[i] = 0;
        f->overlay_from[i] = -1;
    }
    for (i = 0; i < FL_POOL_MAX; i++) { f->pool_busy[i] = 0; }
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

/* Empty every list and release the whole pool after the completed frame is presented. */
static inline void fl_clear(FrameLife *f)
{
    int i;
    for (i = 0; i < f->nlists; i++) {
        f->n[i] = 0;
        f->overlay_from[i] = -1;
    }
    for (i = 0; i < f->pool_n; i++) f->pool_busy[i] = 0;
    f->tile_used = 0;
    f->tiles_this_frame = 0;
}

/* Presentation consumes the display list immediately. Any records LF2 emits after its
 * mid-update present therefore build the next ordinary frame into an already-empty list. */
static inline void fl_frame_reset(FrameLife *f) { fl_clear(f); }

/* The port is about to draw its own UI over a LIVE frame -- the controls hint on a menu the
 * game is still updating. The boundary does not move once set: the hint marks itself on every
 * glyph and only the first is the boundary. */
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
        if (best < 0 || (long)f->pool_w[i] * f->pool_h[i] < (long)f->pool_w[best] * f->pool_h[best]) best = i;
    }
    if (best >= 0) f->pool_busy[best] = 1;
    return best;
}

/* Add a texture of the bucketed size for a tile that nothing free could serve, claimed. -1
 * when the pool is full, which the caller must report as text MISSING from the frame. */
static inline int fl_pool_add(FrameLife *f, int tw, int th)
{
    if (f->pool_n >= FL_POOL_MAX) {
        f->pool_exhausted++;
        return -1;
    }
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
