#ifndef BACKDROP_H
#define BACKDROP_H

typedef struct {
    int dl, dt, dr, db;
    int sl, st, sr, sb;
    int mirror_x;
} BackdropBlit;

typedef struct {
    int dl, dt, dr, db;
    int source_x;
} BackdropBand;

enum {
    BACKDROP_MIRROR_LEFT = 1,
    BACKDROP_MIRROR_RIGHT = 2,
    BACKDROP_EXTEND_BOTTOM = 4,
};

/* Edge continuation is a colour clamp, not an image transform: every output pixel beside
 * the picture takes the exact colour of the adjacent opaque source-edge pixel on that row.
 * The pure geometry lives here so the production painter and the coverage test share it. */
static inline int backdrop_side_band(int enabled, int dst_w, int side, int dl, int dt, int dr, int db, int sl, int sr,
                                     BackdropBand *out)
{
    if (!enabled || !out || dst_w <= 0 || dr <= dl || db <= dt || sr <= sl || (side != -1 && side != 1)) return 0;
    if (side < 0) {
        if (dl <= 0) return 0;
        *out = (BackdropBand){0, dt, dl < dst_w ? dl : dst_w, db, sl};
    } else {
        if (dr >= dst_w) return 0;
        *out = (BackdropBand){dr > 0 ? dr : 0, dt, dst_w, db, sr - 1};
    }
    return out->dr > out->dl;
}

/* Continue only an explicitly authored outer edge. Every segment has exactly the source
 * bitmap's native width/height (or an equally clipped width at the viewport); alternate
 * segments reverse X so their shared edge texels match without stretching or a hard seam. */
static inline int backdrop_mirror_segment(int flags, int dst_w, int segment, int dl, int dt, int dr, int db, int sl,
                                          int st, int sr, int sb, BackdropBlit *out)
{
    if (!out || dst_w <= 0 || segment < 0 || dr <= dl || db <= dt || sr <= sl || sb <= st || dr - dl != sr - sl ||
        db - dt != sb - st)
        return 0;
    const int width = sr - sl;
    if (flags & BACKDROP_MIRROR_RIGHT) {
        const int x = dr + segment * width;
        if (x >= dst_w) return 0;
        const int copy_w = x + width <= dst_w ? width : dst_w - x;
        const int mirror = !(segment & 1);
        const int source_l = mirror ? sr - copy_w : sl;
        *out = (BackdropBlit){x, dt, x + copy_w, db, source_l, st, source_l + copy_w, sb, mirror};
        return 1;
    }
    if (!(flags & BACKDROP_MIRROR_LEFT)) return 0;
    const int x = dl - (segment + 1) * width;
    if (x + width <= 0) return 0;
    const int dest_l = x > 0 ? x : 0;
    const int copy_w = x > 0 ? width : x + width;
    const int mirror = !(segment & 1);
    const int source_l = mirror ? sl : sr - copy_w;
    *out = (BackdropBlit){dest_l, dt, dest_l + copy_w, db, source_l, st, source_l + copy_w, sb, mirror};
    return 1;
}

/* Repeat one native-sized final source row below a widened backdrop. Returning one destination
 * row at a time is deliberate: a single tall destination rectangle made DirectDraw/GPU stretch
 * that row, violating the no-scaling contract even though every sampled pixel happened to match. */
static inline int backdrop_bottom_row(int enabled, int dst_h, int row, int dl, int dt, int dr, int db, int sl, int st,
                                      int sr, int sb, BackdropBlit *out)
{
    (void)dt;
    if (!enabled || !out || dr <= dl || row < db || row >= dst_h || sr <= sl || sb <= st) return 0;
    *out = (BackdropBlit){dl, row, dr, row + 1, sl, sb - 1, sr, sb, 0};
    return 1;
}

#endif
