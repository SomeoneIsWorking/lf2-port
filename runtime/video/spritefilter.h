/* THE OBJECT-SPRITE SAMPLING CHAIN (issue #112), as pure arithmetic.
 *
 * WHAT THIS IS. A fighter is magnified about twice at the window's resolution, and sampling
 * the art with NEAREST leaves every silhouette and interior colour edge staircased. The port
 * lets a player build an ordered CHAIN of resampling passes over the sprite art, plus three
 * terminal steps, and the engine evaluates the whole chain analytically in quad.frag:
 *
 *   scale passes   0..SPRITE_PASS_MAX of them, each a factor and a filter. `nearest:1/2`
 *                  halves the art's resolution by dropping texels; `nearest:2` replaces each
 *                  texel with a 2x2 block; `linear:3` resamples bilinearly. The chain does
 *                  NOT change how big the sprite is on screen -- the quad is untouched -- it
 *                  changes the resolution of the picture that is sampled onto it, which is
 *                  what makes `nearest:1/2` read as chunky pixel art.
 *   aa             edge smoothing: the staircase a diagonal was drawn as is recovered as the
 *                  line it implies and antialiased along it. Flat runs, straight edges and
 *                  single-pixel detail are untouched. See the rule below.
 *   inner          a narrow translucent contour on the ART side of the silhouette. It masks
 *                  the high-contrast boundary texel without changing alpha, growing the
 *                  sprite, or darkening the rest of the picture.
 *   outline        a coloured border of N chain pixels drawn where the chain image is
 *                  transparent but a neighbour within N is not -- the silhouette itself,
 *                  which hides the staircase without softening the interior.
 *
 * WHY THE FACTORS ARE NOT ARBITRARY FLOATS. They are rationals (num/den) so that the exact
 * cases a player asks for -- a half, a third, an integer -- are exact, and so the config
 * string round-trips. AUTO (num == 0) is the round magnification of the quad, measured per
 * draw: `nearest:auto` followed by `aa` is classic integer supersampling.
 *
 * WHY THE CAPS ARE WHAT THEY ARE. quad.frag evaluates the chain per fragment with no
 * intermediate targets, so a LINEAR pass costs four taps of everything before it and `aa`
 * reads a 3x3 neighbourhood of the whole chain: one linear pass is 4 taps, or 36 under aa;
 * two would be 16 and 144. The cap is ONE, and the deciding cost is not only the fragment -- the
 * shader ships as committed SPIR-V and MSL, the optimiser inlines every tap site, and the
 * second linear pass was measured taking the SPIR-V from 39 KB to 174 KB. A chain still has
 * two places to smooth (one pass mid-chain, `aa` at the screen); it may not have three. A
 * spec asking for more is REFUSED by name, never silently trimmed.
 *
 * WHY IT IS A HEADER. engine.c is near its line budget, and every rule here -- what parses,
 * what an AUTO factor resolves to, when a quad must draw alone, how far the geometry grows to
 * make room for an outline -- is the kind of claim that costs a three-minute GPU run to check
 * and a millisecond to assert. tests/test_spritefilter.c walks it.
 *
 * THE OWN-DRAW RULE. Every non-trivial mode clamps its taps into the quad's OWN uv rect --
 * sheets butt frames edge to edge, so the rect cannot be batch-level state. A filtered object
 * sprite therefore always draws alone; everything else batches as before.
 */
#ifndef LF2_SPRITEFILTER_H
#define LF2_SPRITEFILTER_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SPRITE_PASS_MAX = 8,    /* ordered scale passes a chain may hold */
    SPRITE_LINEAR_MAX = 1,  /* of those, how many may be bilinear -- see the cost note above */
    SPRITE_FACTOR_MAX = 8,  /* a factor's numerator and denominator both stop here */
    SPRITE_OUTLINE_MAX = 2, /* outline thickness, in chain pixels */
};

enum { SPRITE_NEAREST = 0, SPRITE_LINEAR = 1 };

/* One scale pass. `num == 0` is AUTO: resolved per draw from the quad's own magnification. */
typedef struct {
    int kind;
    int num;
    int den;
} SpritePass;

typedef struct {
    SpritePass pass[SPRITE_PASS_MAX];
    int count;
    int smooth;  /* terminal edge-only contour reconstruction */
    int inner;   /* terminal narrow contour inside the authored silhouette */
    int outline; /* 0 = off, else thickness in chain pixels */
    float outline_rgb[3];
} SpriteChain;

static inline void spritechain_clear(SpriteChain *c)
{
    memset(c, 0, sizeof *c);
    /* Black is the outline the issue asks for, and black is also the only colour that is
     * invariant under the vertex tint every quad is multiplied by. */
    c->outline_rgb[0] = c->outline_rgb[1] = c->outline_rgb[2] = 0.0f;
}

static inline int spritechain_active(const SpriteChain *c)
{ return c->count > 0 || c->smooth || c->inner || c->outline; }

/* True when this quad must carry the per-draw sampling uniform (and so draw alone). Host tiles
 * are outline-font and SVG coverage, already rasterised at output scale and sampled linearly;
 * the chain is about guest pixel art. */
static inline int spritechain_needs_own_draw(const SpriteChain *c, int is_object, int host_argb)
{ return spritechain_active(c) && is_object && !host_argb; }

/* What an AUTO factor resolves to for one quad: the round magnification of the quad's source
 * span onto its destination, which is the view's world scale measured where it applies. Never
 * below 1 -- a factor under one here would be minification, and the caller asked to supersample. */
static inline float spritechain_auto_factor(float span_w, float span_h, float dest_w, float dest_h)
{
    if (span_w <= 0.0f || span_h <= 0.0f || dest_w <= 0.0f || dest_h <= 0.0f) return 1.0f;
    const float s = 0.5f * (dest_w / span_w + dest_h / span_h);
    const float n = floorf(s + 0.5f);
    if (n < 1.0f) return 1.0f;
    if (n > (float)SPRITE_FACTOR_MAX) return (float)SPRITE_FACTOR_MAX;
    return n;
}

/* Which pass the shader treats as its LINEAR cut, or `count` when the chain has none. The
 * parser holds the chain to one, so this is the first (and only) linear pass. */
static inline int spritechain_linear_cut(const SpriteChain *c)
{
    for (int i = 0; i < c->count; i++)
        if (c->pass[i].kind == SPRITE_LINEAR) return i;
    return c->count;
}

/* One pass's factor as the shader wants it, with AUTO already resolved. */
static inline float spritechain_pass_factor(const SpritePass *p, float auto_factor)
{
    if (p->num == 0) return auto_factor;
    if (p->den <= 0) return (float)p->num;
    return (float)p->num / (float)p->den;
}

/* The resolution of the chain image relative to the source art: the product of every pass. */
static inline float spritechain_total_factor(const SpriteChain *c, float auto_factor)
{
    float f = 1.0f;
    for (int i = 0; i < c->count; i++) f *= spritechain_pass_factor(&c->pass[i], auto_factor);
    return f > 0.0f ? f : 1.0f;
}

/* WHAT `aa` ACTUALLY DOES (issue #113): it RECONSTRUCTS edges, it does not resample.
 *
 * THE FIRST ATTEMPT AND WHY IT WAS WRONG. `aa` began as a plain bilinear of the chain image,
 * which ramps from one chain pixel's centre to the next -- a whole cell wide, so at ~2x every
 * pixel of a fighter sits in a gradient and the art is soft everywhere. Narrowing that ramp to
 * one screen pixel fixed the softness and delivered NOTHING: a box filter of a step edge blends
 * only when the step falls strictly inside a pixel, and at integer magnification texel
 * boundaries land on pixel boundaries, so every weight came out 0 or 1 and `aa` was nearest.
 *
 * The reason no filter of the chain image can work: the staircase is not sampling error. It is
 * in the ART, at the art's own resolution -- the pixel artist drew a diagonal as steps. Sampling
 * it more cleverly reproduces the steps more cleanly. The staircase can only go away if the
 * DIAGONAL THE STEPS IMPLY is recovered and drawn as a line.
 *
 * THE REPLACEMENT RULE. quad.frag reads a 3x3 neighbourhood. Its alpha-aware perceptual
 * distance groups shaded pixels into one continuing edge region without requiring exact byte
 * equality. For each corner, the two outside neighbours must agree and differ from E, the
 * outside diagonal must continue them, and E must continue on an inside axis; that last guard
 * preserves isolated one-pixel detail. Adjacent continuation extends one of the reconstructed
 * line's intercepts, so shallow and steep runs are not forced into a 45-degree cut. Only the
 * resulting wedge is blended; everywhere else returns E exactly.
 *
 * `spritechain_edge_coverage` is the pure geometry seam shared with the offline test. `u,v`
 * are measured from the corner, `extent_u,v` are the reconstructed line's intercepts, and
 * `ramp_u,v` are one output fragment's footprint in chain pixels. */
static inline float spritechain_edge_coverage(float u, float v, float extent_u, float extent_v, float ramp_u,
                                              float ramp_v)
{
    if (extent_u < 1e-6f) extent_u = 1e-6f;
    if (extent_v < 1e-6f) extent_v = 1e-6f;
    float footprint = ramp_u / extent_u + ramp_v / extent_v;
    if (footprint < 1e-6f) footprint = 1e-6f;
    const float t = (1.0f - u / extent_u - v / extent_v) / footprint + 0.5f;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

/* How far a quad's geometry and uv must grow, in SOURCE TEXELS per side, for the outline to
 * have pixels to live on: the outline is `outline` chain pixels, and a chain pixel is 1/F
 * source texels. Zero without an outline, so nothing else moves. */
static inline float spritechain_margin_texels(const SpriteChain *c, float auto_factor)
{
    if (!c->outline) return 0.0f;
    return (float)c->outline / spritechain_total_factor(c, auto_factor);
}

/* ---- the config string ------------------------------------------------------------------
 *
 * `nearest:1/2,nearest:2,aa,inner,outline:1` -- passes in order, then the terminal steps. Empty (or
 * absent) is the original picture. A token that does not parse REFUSES the whole spec: a
 * sampling chain silently missing the pass a player asked for is worse than one that says so. */

static inline int spritechain_parse_factor(const char *s, int *num, int *den)
{
    if (strcmp(s, "auto") == 0) {
        *num = 0;
        *den = 1;
        return 1;
    }
    char *end = NULL;
    const long n = strtol(s, &end, 10);
    if (end == s || n < 1 || n > SPRITE_FACTOR_MAX) return 0;
    long d = 1;
    if (*end == '/') {
        const char *ds = end + 1;
        d = strtol(ds, &end, 10);
        if (end == ds || d < 1 || d > SPRITE_FACTOR_MAX) return 0;
    }
    if (*end != '\0') return 0;
    if (n == d) return 0; /* a 1:1 pass is not a pass; say so rather than quantising for free */
    *num = (int)n;
    *den = (int)d;
    return 1;
}

static inline int spritechain_fail(char *err, size_t errsz, const char *what, const char *token)
{
    if (err && errsz) snprintf(err, errsz, "%s: '%s'", what, token);
    return 0;
}

static inline int spritechain_parse(const char *spec, SpriteChain *out, char *err, size_t errsz)
{
    SpriteChain c;
    spritechain_clear(&c);
    if (err && errsz) err[0] = '\0';
    if (!spec) {
        *out = c;
        return 1;
    }
    int linear = 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ') tok++;
        char *tail = tok + strlen(tok);
        while (tail > tok && tail[-1] == ' ') *--tail = '\0';
        if (!*tok) continue;
        char *colon = strchr(tok, ':');
        const char *arg = colon ? colon + 1 : NULL;
        if (colon) *colon = '\0';
        if (strcmp(tok, "aa") == 0) {
            if (arg) return spritechain_fail(err, errsz, "aa takes no argument", arg);
            c.smooth = 1;
            continue;
        }
        if (strcmp(tok, "inner") == 0) {
            if (arg) return spritechain_fail(err, errsz, "inner takes no argument", arg);
            c.inner = 1;
            continue;
        }
        if (strcmp(tok, "outline") == 0) {
            long t = 1;
            if (arg) {
                char *end = NULL;
                t = strtol(arg, &end, 10);
                if (end == arg || *end != '\0') return spritechain_fail(err, errsz, "outline thickness", arg);
            }
            if (t < 1 || t > SPRITE_OUTLINE_MAX)
                return spritechain_fail(err, errsz, "outline thickness", arg ? arg : tok);
            c.outline = (int)t;
            continue;
        }
        const int kind =
            strcmp(tok, "nearest") == 0 ? SPRITE_NEAREST : (strcmp(tok, "linear") == 0 ? SPRITE_LINEAR : -1);
        if (kind < 0) return spritechain_fail(err, errsz, "unknown sprite pass", tok);
        if (!arg) return spritechain_fail(err, errsz, "pass needs a factor", tok);
        if (c.count >= SPRITE_PASS_MAX) return spritechain_fail(err, errsz, "more passes than SPRITE_PASS_MAX", tok);
        if (kind == SPRITE_LINEAR && ++linear > SPRITE_LINEAR_MAX)
            return spritechain_fail(err, errsz, "more linear passes than SPRITE_LINEAR_MAX", tok);
        SpritePass p = {kind, 0, 1};
        if (!spritechain_parse_factor(arg, &p.num, &p.den)) return spritechain_fail(err, errsz, "pass factor", arg);
        c.pass[c.count++] = p;
    }
    *out = c;
    return 1;
}

/* The inverse, so what the menu built is what the config file says. */
static inline void spritechain_format(const SpriteChain *c, char *buf, size_t n)
{
    size_t used = 0;
    buf[0] = '\0';
    for (int i = 0; i < c->count; i++) {
        char one[32];
        const char *kind = c->pass[i].kind == SPRITE_LINEAR ? "linear" : "nearest";
        if (c->pass[i].num == 0) snprintf(one, sizeof one, "%s:auto", kind);
        else if (c->pass[i].den == 1) snprintf(one, sizeof one, "%s:%d", kind, c->pass[i].num);
        else snprintf(one, sizeof one, "%s:%d/%d", kind, c->pass[i].num, c->pass[i].den);
        used += (size_t)snprintf(buf + used, used < n ? n - used : 0, "%s%s", used ? "," : "", one);
    }
    if (c->smooth) used += (size_t)snprintf(buf + used, used < n ? n - used : 0, "%saa", used ? "," : "");
    if (c->inner) used += (size_t)snprintf(buf + used, used < n ? n - used : 0, "%sinner", used ? "," : "");
    if (c->outline) (void)snprintf(buf + used, used < n ? n - used : 0, "%soutline:%d", used ? "," : "", c->outline);
}

/* ---- what the menu shows and how it edits -------------------------------------------------
 *
 * The factor cycle is the list a player steps through on one pass row. It runs from the
 * coarsest reduction to the largest magnification with AUTO at the end, and it is here rather
 * than in settings_ui.cpp so the menu cannot drift from what parses. */
static inline const char *spritechain_kind_label(const SpritePass *p)
{ return p->kind == SPRITE_LINEAR ? "SMOOTH" : "PIXEL"; }

static inline void spritechain_factor_label(const SpritePass *p, char *buf, size_t n)
{
    if (p->num == 0) snprintf(buf, n, "AUTO");
    else if (p->den == 1) snprintf(buf, n, "%dx", p->num);
    else snprintf(buf, n, "%d/%dx", p->num, p->den);
}

static inline void spritechain_cycle_factor(SpritePass *p)
{
    static const int NUM[] = {1, 1, 1, 2, 3, 4, 0};
    static const int DEN[] = {4, 3, 2, 1, 1, 1, 1};
    const int n = (int)(sizeof NUM / sizeof *NUM);
    int at = n - 1;
    for (int i = 0; i < n; i++)
        if (NUM[i] == p->num && DEN[i] == p->den) at = i;
    const int next = (at + 1) % n;
    p->num = NUM[next];
    p->den = DEN[next];
}

/* Steps one pass row's filter, refusing to create a third linear pass -- the cost cap is a
 * property of the chain, so the menu asks the chain rather than reimplementing the rule. */
static inline void spritechain_cycle_kind(SpriteChain *c, int index)
{
    if (index < 0 || index >= c->count) return;
    if (c->pass[index].kind == SPRITE_LINEAR) {
        c->pass[index].kind = SPRITE_NEAREST;
        return;
    }
    int linear = 0;
    for (int i = 0; i < c->count; i++)
        if (c->pass[i].kind == SPRITE_LINEAR) linear++;
    if (linear < SPRITE_LINEAR_MAX) c->pass[index].kind = SPRITE_LINEAR;
}

static inline int spritechain_add_pass(SpriteChain *c)
{
    if (c->count >= SPRITE_PASS_MAX) return 0;
    const SpritePass p = {SPRITE_NEAREST, 2, 1};
    c->pass[c->count++] = p;
    return 1;
}

static inline int spritechain_remove_pass(SpriteChain *c, int index)
{
    if (index < 0 || index >= c->count) return 0;
    for (int i = index; i + 1 < c->count; i++) c->pass[i] = c->pass[i + 1];
    c->count--;
    return 1;
}

#endif
