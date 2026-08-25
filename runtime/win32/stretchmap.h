/* The StretchBlt source-pick sequence, as pure arithmetic.
 *
 * h_StretchBlt's inner loop picks, for destination column x, the source column
 * floor(x * sw / dw). Written that way the pick costs a 64-bit division per pixel, and the
 * blit of every sprite sheet at load time paid it. This header walks the same sequence with
 * an add and a compare: floor is constant between multiples, so each step either keeps the
 * quotient or adds one, and a running remainder says which.
 *
 * EXACTNESS IS THE WHOLE POINT. A fixed-point accumulator drifts by a texel on long rows,
 * which would move sprite content; tests/test_stretchmap.c sweeps sizes against the division
 * itself, so the two cannot quietly come apart. */
#ifndef LF2_STRETCHMAP_H
#define LF2_STRETCHMAP_H

typedef struct {
    int whole; /* sw / dw: the guaranteed step per destination pixel */
    int frac;  /* sw % dw: the extra source pixel owed once the remainder fills */
    int n;     /* the number of picks this sequence produces (dw) */
} StretchMap;

typedef struct {
    int at;        /* the current pick: floor(x * sw / dw) */
    unsigned rest; /* accumulated remainder, always < dw */
} StretchMapPos;

static inline StretchMap stretchmap_begin(int src, int dst)
{
    StretchMap m = {0, 0, 1};
    if (dst <= 0 || src < 0) return m; /* degenerate: every pick stays 0 */
    m.whole = src / dst;
    m.frac = src % dst;
    m.n = dst;
    return m;
}

static inline StretchMapPos stretchmap_start(const StretchMap *m)
{
    StretchMapPos p = {0, 0};
    (void)m;
    return p;
}

static inline void stretchmap_next(const StretchMap *m, StretchMapPos *p)
{
    p->at += m->whole;
    p->rest += (unsigned)m->frac;
    if (p->rest >= (unsigned)m->n) {
        p->rest -= (unsigned)m->n;
        p->at += 1;
    }
}

#endif
