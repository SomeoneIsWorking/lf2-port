#ifndef BACKDROP_H
#define BACKDROP_H

#include <stdint.h>

typedef struct {
    int dl, dt, dr, db;
    int sl, st, sr, sb;
} BackdropBlit;

typedef struct {
    int span;
    int x;
    int y;
    int loop;
    int tint;
} BackdropLayerLayout;

/* A widened stage backdrop still ends at its original bottom row. Later keyed layers were
 * authored to cover that edge in a 794-wide view, but reveal black holes when a wide view
 * exposes the gaps between them. Continue the backdrop's own final row behind those layers;
 * this extends existing edge pixels and does not stretch a prop or invent another tile. */
static inline int backdrop_bottom_extension(int enabled, int dst_h, int dl, int dt, int dr, int db, int sl, int st,
                                            int sr, int sb, BackdropBlit *out)
{
    (void)dt;
    if (!enabled || !out || dr <= dl || db >= dst_h || sr <= sl || sb <= st) return 0;
    *out = (BackdropBlit){dl, db, dr, dst_h, sl, sb - 1, sr, sb};
    return 1;
}

/* Non-looping layer art has no additional picture beyond its authored span. Once the live
 * view is wider than that span, scale every piece in the span by the same ratio. Shared
 * boundaries therefore remain shared, while the span's outside edges move to the viewport
 * edges where they cannot appear as vertical cutoffs. */
static inline int backdrop_scale_span(int span, int view, int dl, int dr, int *scaled_l, int *scaled_r)
{
    if (!scaled_l || !scaled_r || span <= 0 || view <= span || dl < 0 || dr < dl || dr > span) return 0;
    *scaled_l = (int)((int64_t)dl * view / span);
    *scaled_r = (int)((int64_t)dr * view / span);
    return 1;
}

/* A plane is background coverage rather than an isolated prop when its non-looping pieces
 * share one parallax span, start at the span's left edge, and occupy distinct authored X
 * positions above the walkable floor. The first layer is independently known to be the far
 * painted backdrop. This keeps CUHK's lamps/grass and other isolated foreground decorations
 * at native size while allowing multi-piece skies and mountain bands to adapt together. */
static inline int backdrop_plane_span(const BackdropLayerLayout *layers, int count, int index, int view, int floor_top)
{
    if (!layers || index < 0 || index >= count) return 0;
    const BackdropLayerLayout *layer = &layers[index];
    if (layer->span <= 0 || layer->span >= view || layer->loop > 0 || layer->tint) return 0;
    if (floor_top > 0 && layer->y >= floor_top) return 0;
    if (index == 0) return layer->span;
    if (floor_top <= 0) return 0;

    int min_x = layer->x;
    int distinct_x = 0;
    for (int i = 0; i < count; i++) {
        const BackdropLayerLayout *peer = &layers[i];
        if (peer->span != layer->span || peer->loop > 0 || peer->tint) continue;
        if (peer->x < min_x) min_x = peer->x;
        if (peer->x != layer->x) distinct_x = 1;
    }
    return min_x <= 0 && distinct_x ? layer->span : 0;
}

#endif
