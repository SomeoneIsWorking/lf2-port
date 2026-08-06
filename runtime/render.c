/* The native renderer -- see runtime/render.h for what it is and why it exists. */

#include "render.h"
#include "guest.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the display list ----
 *
 * One list per destination surface, because that is how a frame is recognised: the game
 * composes into an off-screen surface and copies it to the primary, so the source of that
 * copy names the composition without anyone hardcoding it.
 *
 * The caps are generous against measured reality -- a match frame is about 140 entries and
 * the busiest screen a few hundred -- and OVERFLOW IS COUNTED AND REPORTED rather than
 * silently dropped. A renderer that quietly stops drawing at entry 4096 would look like a
 * rendering bug anywhere but here.
 */
enum { LIST_MAX = 8192, SURF_MAX = 8, TEX_MAX = 512, TILE_BYTES_MAX = 4u << 20 };

enum EntryKind { E_TEX, E_FILL, E_TILE };

typedef struct {
    int kind;
    SDL_FRect dst;
    /* E_TEX */
    uint32_t src_pixels;
    SDL_FRect src;
    int      keyed;
    uint32_t key_lo, key_hi;
    int      sw, sh, spitch;
    /* E_FILL */
    uint32_t argb;
    /* E_TILE */
    uint32_t tile_off;              /* byte offset into the frame's tile arena */
    int      tw, th;
} Entry;

typedef struct {
    uint32_t dst_pixels;
    Entry   *e;
    int      n;
    long     dropped;
} List;

/* A cached GPU texture for one guest surface. Keyed on the guest pixel address AND the
 * colour key, because the same sheet drawn keyed and unkeyed is two different textures once
 * the key has been turned into alpha. */
typedef struct {
    uint32_t pixels;
    int      keyed;
    uint32_t key_lo, key_hi;
    int      w, h;
    uint32_t content;               /* hash of the source when uploaded */
    SDL_Texture *tex;
    long     uploads;
} Tex;

static SDL_Renderer *R;
static List   lists[SURF_MAX];
static int    nlists;
static Tex    texes[TEX_MAX];
static int    ntexes;

static uint8_t *tile_arena;
static uint32_t tile_used;
static List    *tile_list;          /* the list the open tile belongs to */
static int      tile_index;

static SDL_Texture *target;         /* the render target the frame is drawn into */
static int          target_w, target_h;

static long stat_frames, stat_tex, stat_fill, stat_tile, stat_uploads, stat_dropped;
static long stat_soft_frames, stat_post;

static void post_free(void);

int render_gpu_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("LF2_RENDERER");
        on = !(v && (strcmp(v, "soft") == 0 || strcmp(v, "software") == 0));
    }
    return on;
}

void render_init(SDL_Renderer *r)
{
    R = r;
    if (!render_gpu_enabled()) return;
    if (!tile_arena) tile_arena = malloc(TILE_BYTES_MAX);
}

void render_shutdown(void)
{
    for (int i = 0; i < ntexes; i++)
        if (texes[i].tex) { SDL_DestroyTexture(texes[i].tex); texes[i].tex = NULL; }
    ntexes = 0;
    if (target) { SDL_DestroyTexture(target); target = NULL; }
    post_free();
    for (int i = 0; i < nlists; i++) { free(lists[i].e); lists[i].e = NULL; }
    nlists = 0;
    free(tile_arena); tile_arena = NULL;
}

static List *list_for(uint32_t dst_pixels)
{
    for (int i = 0; i < nlists; i++)
        if (lists[i].dst_pixels == dst_pixels) return &lists[i];
    if (nlists >= SURF_MAX) return NULL;
    List *l = &lists[nlists++];
    l->dst_pixels = dst_pixels;
    l->e = calloc(LIST_MAX, sizeof *l->e);
    l->n = 0;
    return l->e ? l : NULL;
}

static Entry *entry_push(uint32_t dst_pixels)
{
    List *l = list_for(dst_pixels);
    if (!l) return NULL;
    if (l->n >= LIST_MAX) { l->dropped++; stat_dropped++; return NULL; }
    Entry *e = &l->e[l->n++];
    memset(e, 0, sizeof *e);
    return e;
}

/* ---- textures ----
 *
 * A sheet is loaded once and then drawn thousands of times, so the upload is cached. The
 * cache is validated by a CONTENT HASH rather than trusted: a surface the game re-loads into
 * the same arena slot would otherwise keep drawing the old picture for the rest of the run,
 * which is a bug that looks like corrupted art and would be very hard to trace back here.
 * The hash is sampled (every 7th row) -- enough to catch a different picture, cheap enough to
 * run per draw.
 */
static uint32_t sample_hash(const uint8_t *base, int w, int h, int pitch)
{
    uint32_t x = 2166136261u;
    for (int y = 0; y < h; y += 7) {
        const uint32_t *row = (const uint32_t *)(base + (size_t)y * (size_t)pitch);
        for (int i = 0; i < w; i += 5) { x ^= row[i]; x *= 16777619u; }
    }
    x ^= (uint32_t)w * 2654435761u; x ^= (uint32_t)h * 40503u;
    return x;
}

/* The colour key becomes ALPHA. This is where this port's blend stage comes from: the
 * software blitter could only skip a keyed pixel, so nothing could ever be partly
 * transparent; a keyed source uploaded as RGBA can be, and that is what a cast shadow and
 * every HD2D pass need. */
static void upload(Tex *t, const uint8_t *base, int w, int h, int pitch)
{
    void *px = NULL;
    int   dp = 0;
    if (!SDL_LockTexture(t->tex, NULL, &px, &dp)) return;
    const uint32_t lo = t->key_lo & 0x00ffffffu, hi = t->key_hi & 0x00ffffffu;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)(base + (size_t)y * (size_t)pitch);
        uint32_t *dst = (uint32_t *)((uint8_t *)px + (size_t)y * (size_t)dp);
        if (!t->keyed) {
            for (int x = 0; x < w; x++) dst[x] = 0xff000000u | (src[x] & 0x00ffffffu);
        } else {
            for (int x = 0; x < w; x++) {
                const uint32_t v = src[x] & 0x00ffffffu;
                dst[x] = (v >= lo && v <= hi) ? 0u : (0xff000000u | v);
            }
        }
    }
    SDL_UnlockTexture(t->tex);
    t->uploads++;
    stat_uploads++;
}

static Tex *tex_for(uint32_t pixels, int w, int h, int pitch,
                    int keyed, uint32_t key_lo, uint32_t key_hi)
{
    if (w <= 0 || h <= 0) return NULL;
    const uint8_t *base = g_mem + pixels;
    const uint32_t content = sample_hash(base, w, h, pitch);

    for (int i = 0; i < ntexes; i++) {
        Tex *t = &texes[i];
        if (t->pixels != pixels || t->keyed != keyed || t->w != w || t->h != h) continue;
        if (keyed && (t->key_lo != key_lo || t->key_hi != key_hi)) continue;
        if (t->content != content) { t->content = content; upload(t, base, w, h, pitch); }
        return t;
    }
    if (ntexes >= TEX_MAX) return NULL;
    Tex *t = &texes[ntexes];
    t->tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!t->tex) return NULL;
    SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t->tex, keyed ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    t->pixels = pixels; t->keyed = keyed; t->key_lo = key_lo; t->key_hi = key_hi;
    t->w = w; t->h = h; t->content = content;
    ntexes++;
    upload(t, base, w, h, pitch);
    return t;
}

void render_surface_dirty(uint32_t pixels)
{
    for (int i = 0; i < ntexes; i++)
        if (texes[i].pixels == pixels) texes[i].content = 0u;
}

/* ---- recording ---- */

void render_blit(uint32_t dst_pixels,
                 int dl, int dt, int dr, int db,
                 uint32_t src_pixels, int sw, int sh, int spitch,
                 int sl, int st, int sr, int sb,
                 int keyed, uint32_t key_lo, uint32_t key_hi)
{
    if (!render_gpu_enabled() || dr <= dl || db <= dt || sr <= sl || sb <= st) return;
    Entry *e = entry_push(dst_pixels);
    if (!e) return;
    e->kind = E_TEX;
    e->dst = (SDL_FRect){ (float)dl, (float)dt, (float)(dr - dl), (float)(db - dt) };
    e->src = (SDL_FRect){ (float)sl, (float)st, (float)(sr - sl), (float)(sb - st) };
    e->src_pixels = src_pixels;
    e->sw = sw; e->sh = sh; e->spitch = spitch;
    e->keyed = keyed; e->key_lo = key_lo; e->key_hi = key_hi;
}

void render_fill(uint32_t dst_pixels, int dl, int dt, int dr, int db, uint32_t argb)
{
    if (!render_gpu_enabled() || dr <= dl || db <= dt) return;
    Entry *e = entry_push(dst_pixels);
    if (!e) return;
    e->kind = E_FILL;
    e->dst = (SDL_FRect){ (float)dl, (float)dt, (float)(dr - dl), (float)(db - dt) };
    e->argb = argb;
}

uint32_t *render_tile_begin(uint32_t dst_pixels, int x, int y, int w, int h)
{
    if (!render_gpu_enabled() || !tile_arena || w <= 0 || h <= 0) return NULL;
    const uint32_t need = (uint32_t)w * (uint32_t)h * 4u;
    if (need > TILE_BYTES_MAX - tile_used) { stat_dropped++; return NULL; }
    List *l = list_for(dst_pixels);
    if (!l || l->n >= LIST_MAX) { if (l) { l->dropped++; stat_dropped++; } return NULL; }
    Entry *e = &l->e[l->n];
    memset(e, 0, sizeof *e);
    e->kind = E_TILE;
    e->dst = (SDL_FRect){ (float)x, (float)y, (float)w, (float)h };
    e->tile_off = tile_used;
    e->tw = w; e->th = h;
    uint32_t *p = (uint32_t *)(tile_arena + tile_used);
    memset(p, 0, need);
    tile_used += need;
    tile_list = l; tile_index = l->n;
    return p;
}

void render_tile_end(void)
{
    if (!tile_list) return;
    tile_list->n = tile_index + 1;      /* commit the entry only now */
    tile_list = NULL;
}

/* ---- drawing ---- */

static SDL_Texture *tile_texture(const Entry *e)
{
    /* Tiles are one-shot: a fresh texture each frame, destroyed after the draw. They are
     * small (a glyph is 8x16) and few, and caching them would need an identity they do not
     * have. */
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, e->tw, e->th);
    if (!t) return NULL;
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    /* PREMULTIPLIED: the writer already multiplied the colour by its coverage, so the blend
     * is src + dst*(1-a) rather than src*a + dst*(1-a). Using plain BLEND here would darken
     * every glyph edge. */
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    void *px = NULL; int dp = 0;
    if (SDL_LockTexture(t, NULL, &px, &dp)) {
        const uint32_t *src = (const uint32_t *)(tile_arena + e->tile_off);
        for (int y = 0; y < e->th; y++)
            memcpy((uint8_t *)px + (size_t)y * (size_t)dp, src + (size_t)y * (size_t)e->tw,
                   (size_t)e->tw * 4);
        SDL_UnlockTexture(t);
    }
    return t;
}

/* LF2_RENDER_SKIP=<n> drops every nth entry. It is the negative arm of
 * tools/render_test.sh: "the GPU frame matches the software frame" would pass just as
 * happily on a comparison that was reading the same buffer twice, which is exactly the bug
 * that hid here once already -- the readback ran before the draw and dumped the previous
 * frame. An arm that deliberately draws the frame WRONG has to come out different, or the
 * match proves nothing. Never set in normal use. */
static int render_skip(void)
{
    static int n = -1;
    if (n < 0) { const char *v = getenv("LF2_RENDER_SKIP"); n = v ? atoi(v) : 0; }
    return n;
}

static void draw_list(const List *l, float offx)
{
    const int skip = render_skip();
    for (int i = 0; i < l->n; i++) {
        if (skip > 0 && (i % skip) == 0) continue;
        const Entry *e = &l->e[i];
        SDL_FRect dst = e->dst;
        dst.x += offx;
        if (e->kind == E_FILL) {
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(R, (uint8_t)(e->argb >> 16), (uint8_t)(e->argb >> 8),
                                   (uint8_t)e->argb, 255);
            SDL_RenderFillRect(R, &dst);
            stat_fill++;
        } else if (e->kind == E_TEX) {
            Tex *t = tex_for(e->src_pixels, e->sw, e->sh, e->spitch,
                             e->keyed, e->key_lo, e->key_hi);
            if (!t) continue;
            SDL_RenderTexture(R, t->tex, &e->src, &dst);
            stat_tex++;
        } else {
            SDL_Texture *t = tile_texture(e);
            if (!t) continue;
            SDL_RenderTexture(R, t, NULL, &dst);
            SDL_DestroyTexture(t);
            stat_tile++;
        }
    }
}

/* ---- the HD2D pass ----
 *
 * A bloom, built out of render targets and blend modes only. SDL 3.4 can take custom shaders
 * through SDL_GPURenderState, but only as precompiled SPIR-V/DXIL/MSL -- that is a shader
 * toolchain in a build that currently needs nothing but a C compiler and SDL, on a port whose
 * whole point is that it builds anywhere. So the bright pass is done with arithmetic the
 * fixed-function blender already has:
 *
 *   BRIGHT PASS   dst = frame * frame, which is SDL_BLENDMODE_MOD of the frame over a copy
 *                 of itself. Squaring in 0..1 leaves highlights near their value and pushes
 *                 midtones down quadratically -- a soft threshold with no branch and no
 *                 shader. A plain additive glow without it just brightens everything and
 *                 looks like a washed-out screen rather than light.
 *   BLUR          successive downsamples with LINEAR filtering, then one upsample. The
 *                 filtering IS the blur; doing it in two steps (1/4 then 1/16) spreads it
 *                 far enough to read as light rather than as a halo.
 *   COMPOSITE     added back over the frame.
 *
 * LF2_HD2D=off turns it off. That is a DIAGNOSTIC, not how the feature is reached: the effect
 * is on by default, because a look nobody can find is not a look. It exists so
 * tools/render_test.sh can compare the renderer's GEOMETRY against the software compositor
 * without the post-process in the way, and so the pass can be shown to do something.
 */
static SDL_Texture *bp_q, *bp_sq, *bp_bloom;
static int bp_w, bp_h;

static int hd2d_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("LF2_HD2D");
        on = !(v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0));
    }
    return on;
}

static int hd2d_strength(void)
{
    static int a = -1;
    if (a < 0) { const char *v = getenv("LF2_HD2D_BLOOM"); a = v ? atoi(v) : 110; }
    return a < 0 ? 0 : (a > 255 ? 255 : a);
}

static SDL_Texture *post_target(SDL_Texture **slot, int w, int h)
{
    if (*slot) return *slot;
    *slot = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (*slot) SDL_SetTextureScaleMode(*slot, SDL_SCALEMODE_LINEAR);
    return *slot;
}

static void post_free(void)
{
    if (bp_q)     { SDL_DestroyTexture(bp_q);     bp_q = NULL; }
    if (bp_sq)    { SDL_DestroyTexture(bp_sq);    bp_sq = NULL; }
    if (bp_bloom) { SDL_DestroyTexture(bp_bloom); bp_bloom = NULL; }
}

/* Draws `src` filling the current target, with one blend mode. */
static void post_draw(SDL_Texture *src, SDL_BlendMode bm, int alpha)
{
    SDL_SetTextureBlendMode(src, bm);
    SDL_SetTextureAlphaMod(src, (uint8_t)alpha);
    SDL_RenderTexture(R, src, NULL, NULL);
    SDL_SetTextureAlphaMod(src, 255);
}

/* Runs the pass over `frame`, leaving the result in `frame`. Returns 0 and changes nothing
 * if any target could not be made -- a failed effect must not cost the player the picture. */
static int hd2d_apply(SDL_Texture *frame, int w, int h)
{
    if (!hd2d_on() || hd2d_strength() == 0) return 0;
    const int qw = w / 4 > 1 ? w / 4 : 1, qh = h / 4 > 1 ? h / 4 : 1;
    const int sw = w / 16 > 1 ? w / 16 : 1, sh = h / 16 > 1 ? h / 16 : 1;
    if (bp_w != w || bp_h != h) { post_free(); bp_w = w; bp_h = h; }
    if (!post_target(&bp_q, qw, qh) || !post_target(&bp_sq, qw, qh)
        || !post_target(&bp_bloom, sw, sh)) return 0;

    SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_LINEAR);

    SDL_SetRenderTarget(R, bp_q);
    post_draw(frame, SDL_BLENDMODE_NONE, 255);

    /* frame*frame. MOD is dst*src, so the copy has to be laid down first. */
    SDL_SetRenderTarget(R, bp_sq);
    post_draw(bp_q, SDL_BLENDMODE_NONE, 255);
    post_draw(bp_q, SDL_BLENDMODE_MOD, 255);

    SDL_SetRenderTarget(R, bp_bloom);
    post_draw(bp_sq, SDL_BLENDMODE_NONE, 255);

    SDL_SetRenderTarget(R, frame);
    post_draw(bp_bloom, SDL_BLENDMODE_ADD, hd2d_strength());

    SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderTarget(R, NULL);
    return 1;
}

int render_present(uint32_t src_pixels, int off, int w, int h)
{
    if (!render_gpu_enabled() || !R) return 0;
    List *l = NULL;
    for (int i = 0; i < nlists; i++) if (lists[i].dst_pixels == src_pixels) l = &lists[i];
    /* No list for this surface means the frame was not built through the routes this
     * renderer captures. Say so once and hand the frame back to the software path rather
     * than presenting an empty screen -- a black frame is the one failure mode that looks
     * like a crash and tells nobody why. */
    if (!l || l->n == 0) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "render: no display list for the composition at %08x -- the "
                            "software compositor is presenting this frame. The GPU path "
                            "records draws per destination surface and this one had none.\n",
                    src_pixels);
        }
        stat_soft_frames++;
        return 0;
    }

    /* THE FRAME IS DRAWN AT THE WINDOW'S RESOLUTION, not the game's.
     *
     * The software compositor has no choice about this: it fills a 794x550 (or as-wide-as-the
     * -aspect) buffer and SDL point-scales that one texture up to the window, so on a 1080p
     * screen every game pixel becomes a 2x2 block and everything -- text, the bloom, anything
     * added later -- is quantised to the small grid before it is ever enlarged.
     *
     * Drawing from a display list removes that constraint: the quads carry the game's own
     * coordinates and the SCALE is applied as they are drawn, so the render target is the size
     * of the output and every later pass runs at full resolution. Sprites are pixel art and
     * still land on the same grid (nearest, integer coordinates), but they are resampled ONCE
     * instead of twice, and the post-process is no longer working on an upscaled thumbnail.
     *
     * Logical presentation is turned off while this happens: it exists to scale the software
     * path's small buffer, and leaving it on would scale the full-resolution frame a second
     * time. */
    int ow = w, oh = h;
    SDL_GetCurrentRenderOutputSize(R, &ow, &oh);
    if (ow <= 0 || oh <= 0) { ow = w; oh = h; }
    SDL_RendererLogicalPresentation lp_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    int lp_w = 0, lp_h = 0;
    SDL_GetRenderLogicalPresentation(R, &lp_w, &lp_h, &lp_mode);
    if (lp_mode != SDL_LOGICAL_PRESENTATION_DISABLED)
        SDL_SetRenderLogicalPresentation(R, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);

    if (target && (target_w != ow || target_h != oh)) {
        SDL_DestroyTexture(target); target = NULL;
    }
    if (!target) {
        /* A render target, which the port has never had. It is what every later pass needs:
         * a finished frame that can be read back and processed rather than one that has
         * already gone to the screen. */
        target = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_TARGET, ow, oh);
        if (!target) {
            if (lp_mode != SDL_LOGICAL_PRESENTATION_DISABLED)
                SDL_SetRenderLogicalPresentation(R, lp_w, lp_h, lp_mode);
            return 0;
        }
        SDL_SetTextureScaleMode(target, SDL_SCALEMODE_NEAREST);
        target_w = ow; target_h = oh;
    }

    SDL_SetRenderTarget(R, target);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    SDL_RenderClear(R);
    SDL_SetRenderScale(R, (float)ow / (float)w, (float)oh / (float)h);
    /* The widescreen centring offset is in the game's coordinates, so it goes on before the
     * scale rather than after -- which is what applying it per rectangle gets for free. */
    draw_list(l, (float)off);
    SDL_SetRenderScale(R, 1.0f, 1.0f);

    /* The frame exists as a texture now, which is the whole reason the render target is
     * here: it can be read back, blurred and composited before anyone sees it. */
    if (hd2d_apply(target, ow, oh)) stat_post++;

    SDL_SetRenderTarget(R, NULL);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    SDL_RenderClear(R);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(R, target, NULL, NULL);
    SDL_RenderPresent(R);
    if (lp_mode != SDL_LOGICAL_PRESENTATION_DISABLED)
        SDL_SetRenderLogicalPresentation(R, lp_w, lp_h, lp_mode);
    stat_frames++;
    if (stat_frames == 1)
        fprintf(stderr, "render: drawing %dx%d of game at %dx%d output (scale %.2fx)\n",
                w, h, ow, oh, (float)ow / (float)w);
    return 1;
}

/* The size the frame was last drawn at, which is the OUTPUT size and not the game's. The
 * dump and readback paths need it because it is no longer the composition's. */
void render_output_size(int *w, int *h) { *w = target_w; *h = target_h; }

/* Read the frame that was actually PRESENTED back off the GPU, so LF2_FRAME_DUMP dumps what
 * the player saw rather than what the software compositor happened to have in the primary.
 * Without this the two paths could not be diffed at all -- every dump would be the software
 * frame no matter which renderer drew the screen, and the comparison would be of a buffer
 * against itself. Returns 0 if there is nothing to read. */
int render_readback(uint32_t *dst, int w, int h, int dst_pitch)
{
    if (!R || !target || target_w != w || target_h != h) return 0;
    SDL_SetRenderTarget(R, target);
    SDL_Surface *sf = SDL_RenderReadPixels(R, NULL);
    SDL_SetRenderTarget(R, NULL);
    if (!sf) return 0;
    SDL_Surface *cv = (sf->format == SDL_PIXELFORMAT_ARGB8888)
                    ? sf : SDL_ConvertSurface(sf, SDL_PIXELFORMAT_ARGB8888);
    int ok = 0;
    {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "render: readback target %dx%d -> surface %dx%d pitch %d "
                            "(asked %dx%d)\n", target_w, target_h, sf->w, sf->h, sf->pitch,
                    w, h);
        }
    }
    if (cv) {
        for (int y = 0; y < h && y < cv->h; y++)
            memcpy((uint8_t *)dst + (size_t)y * (size_t)dst_pitch,
                   (const uint8_t *)cv->pixels + (size_t)y * (size_t)cv->pitch,
                   (size_t)(w < cv->w ? w : cv->w) * 4);
        ok = 1;
        if (cv != sf) SDL_DestroySurface(cv);
    }
    SDL_DestroySurface(sf);
    return ok;
}

/* Reset for the next frame. Called after the present, whichever path drew it -- the lists
 * must not survive into a frame that did not build them. */
void render_frame_reset(void)
{
    for (int i = 0; i < nlists; i++) lists[i].n = 0;
    tile_used = 0;
    tile_list = NULL;
}

/* Prints the zeros too, and says what a zero means. "0 quads" and "the renderer was never
 * called" are different faults with the same look. */
void render_report(void)
{
    if (!getenv("LF2_RENDER_DEBUG")) return;
    fprintf(stderr, "render: gpu=%s frames=%ld (software fallbacks=%ld) quads=%ld fills=%ld "
                    "tiles=%ld textures=%d uploads=%ld dropped=%ld\n",
            render_gpu_enabled() ? "on" : "off", stat_frames, stat_soft_frames,
            stat_tex, stat_fill, stat_tile, ntexes, stat_uploads, stat_dropped);
    fprintf(stderr, "render: hd2d=%s bloom=%d applied to %ld frame(s)\n",
            hd2d_on() ? "on" : "off", hd2d_strength(), stat_post);
    if (hd2d_on() && stat_frames && !stat_post)
        fprintf(stderr, "render: the HD2D pass ran on NO frames although %ld were drawn -- "
                        "its render targets could not be created, so the picture is the "
                        "plain composition\n", stat_frames);
    if (render_gpu_enabled() && stat_frames == 0)
        fprintf(stderr, "render: the GPU path presented NO frames -- every one fell back to "
                        "the software compositor, so these counters describe nothing\n");
    if (stat_dropped)
        fprintf(stderr, "render: %ld entries were DROPPED (list or tile arena full), so the "
                        "frames above are incomplete\n", stat_dropped);
}
