#ifndef BACKDROP_H
#define BACKDROP_H

typedef struct {
    int dl, dt, dr, db;
    int sl, st, sr, sb;
} BackdropBlit;

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

#endif
