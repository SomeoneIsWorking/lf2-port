/* The native renderer -- see runtime/render.h for what it is and why it exists, and
 * runtime/hd2d.h for what is done with the geometry once it is here. */

#include "render.h"
#include "overrides/geom.h"
#include "hd2d.h"
#include "guest.h"
#include "hostwin.h"

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

enum EntryKind { E_TEX, E_FILL, E_TILE, E_GROUND };

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
    SDL_Texture *tile_tex;          /* made once per frame, used by every pass over the list */
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

/* Tile textures are POOLED and reused across frames rather than allocated per frame; see
 * tile_texture below for the GPU reset that made that necessary. */
enum { TILE_TEX_MAX = 128 };
typedef struct {
    SDL_Texture *tex;
    int          w, h;
    int          busy;              /* claimed by an entry in the frame being drawn */
} TileTex;
static TileTex tile_pool[TILE_TEX_MAX];
static int     tile_pool_n;
static long    stat_tile_allocs, stat_tile_exhausted;
static List    *tile_list;          /* the list the open tile belongs to */
static int      tile_index;

static SDL_Texture *target;         /* the frame the player is shown */
static SDL_Texture *rt_albedo;      /* the composition, exactly as the game drew it */
static SDL_Texture *rt_chars;       /* which of those pixels are a fighter, and how high */
static SDL_Texture *rt_shadow;      /* the cast-shadow mask */
static int          target_w, target_h;

static long stat_frames, stat_tex, stat_fill, stat_tile, stat_uploads, stat_dropped;
static long stat_soft_frames, stat_post, stat_ground, stat_shadow;
static float ground_y_lo = 1e9f, ground_y_hi = -1e9f;
static float stat_airborne_max;
static long  stat_ground_orphan;

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
    hd2d_init(r);
}

static void targets_free(void)
{
    SDL_Texture **ts[] = { &target, &rt_albedo, &rt_chars, &rt_shadow };
    for (unsigned i = 0; i < SDL_arraysize(ts); i++)
        if (*ts[i]) { SDL_DestroyTexture(*ts[i]); *ts[i] = NULL; }
    target_w = target_h = 0;
}

void render_shutdown(void)
{
    for (int i = 0; i < ntexes; i++)
        if (texes[i].tex) { SDL_DestroyTexture(texes[i].tex); texes[i].tex = NULL; }
    ntexes = 0;
    for (int i = 0; i < tile_pool_n; i++)
        if (tile_pool[i].tex) { SDL_DestroyTexture(tile_pool[i].tex); tile_pool[i].tex = NULL; }
    tile_pool_n = 0;
    hd2d_shutdown();
    targets_free();
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
 * transparent; a keyed source uploaded as RGBA can be, and that is what a cast shadow, a
 * silhouette in the g-buffer and every lighting term need. */
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
    /* NEAREST, always. This is pixel art magnified two or three times; anything else turns
     * a 32-pixel fighter into a smear. The frame is built at the window's resolution so the
     * sprite is resampled exactly ONCE, here, and every later pass runs on the result at
     * 1:1 -- which is the other half of keeping the pixels crisp. */
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

/* The ground marker. Recorded in the list rather than kept in a side variable so it keeps its
 * ORDER: the ellipse belongs to the sprite drawn immediately after it, and with several
 * objects on screen the pairing is only correct if both stay in sequence. It is also the
 * object's DEPTH, which is the other thing the sprite cannot supply. */
void render_shadow_ground(uint32_t dst_pixels, int dl, int dt, int dr, int db)
{
    if (!render_gpu_enabled() || dr <= dl || db <= dt) return;
    Entry *e = entry_push(dst_pixels);
    if (!e) return;
    e->kind = E_GROUND;
    e->dst = (SDL_FRect){ (float)dl, (float)dt, (float)(dr - dl), (float)(db - dt) };
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

/* ---- tile textures, and why they are POOLED ----
 *
 * GDI text arrives as a few dozen small tiles per frame, and each needs a GPU texture. The
 * first version created one per draw and destroyed it immediately; the second created one per
 * frame and destroyed it in the frame reset. Both are wrong for the same reason, and it took
 * a wedged GPU to see it: a match frame carries about 40 tiles, so at 30 frames a second that
 * is roughly 2400 GPU texture allocations AND frees every second, sustained for the length of
 * a run. On an amdgpu/RADV machine a long batch of those runs ended in 219 ring timeouts and
 * 65 full GPU resets with VRAM loss -- none in the preceding 46 hours of the same boot.
 *
 * So they are POOLED and never freed until shutdown. A tile's size repeats constantly (a
 * glyph run is one of a handful of widths), so after the first few frames the pool is warm
 * and the steady-state allocation count is ZERO. The pool is keyed on exact size, the entries
 * are marked in use for the frame and released by the frame reset, and an exhausted pool
 * falls back to drawing nothing for that tile AND SAYS SO -- silently losing text would look
 * like a font bug anywhere but here.
 */
static SDL_Texture *tile_texture(Entry *e)
{
    if (e->tile_tex) return e->tile_tex;

    SDL_Texture *t = NULL;
    for (int i = 0; i < tile_pool_n; i++) {
        TileTex *p = &tile_pool[i];
        if (p->busy || p->w != e->tw || p->h != e->th) continue;
        p->busy = 1;
        t = p->tex;
        break;
    }
    if (!t) {
        if (tile_pool_n >= TILE_TEX_MAX) {
            stat_tile_exhausted++;
            return NULL;
        }
        t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, e->tw, e->th);
        if (!t) { stat_tile_exhausted++; return NULL; }
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        /* PREMULTIPLIED: the writer already multiplied the colour by its coverage, so the
         * blend is src + dst*(1-a) rather than src*a + dst*(1-a). Using plain BLEND here
         * would darken every glyph edge. */
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        tile_pool[tile_pool_n++] = (TileTex){ t, e->tw, e->th, 1 };
        stat_tile_allocs++;
    }

    void *px = NULL; int dp = 0;
    if (SDL_LockTexture(t, NULL, &px, &dp)) {
        const uint32_t *src = (const uint32_t *)(tile_arena + e->tile_off);
        for (int y = 0; y < e->th; y++)
            memcpy((uint8_t *)px + (size_t)y * (size_t)dp, src + (size_t)y * (size_t)e->tw,
                   (size_t)e->tw * 4);
        SDL_UnlockTexture(t);
    }
    e->tile_tex = t;
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

/* ---- cast shadows ----
 *
 * The game's shadow is one flat dithered ellipse per object, the same bitmap wherever the
 * object is and whatever shape it is in. Magnified it reads as a checkerboard, because the
 * dither was chosen for a 794x550 screen.
 *
 * A real one is the SPRITE's own silhouette, projected onto the ground away from the light.
 * It is drawn as a sheared quad through SDL_RenderGeometry -- SDL_RenderTexture cannot shear,
 * and without the shear a squashed sprite reads as a reflection rather than a shadow.
 * Nothing about its shape is a constant: hd2d_shadow_project() gives where a point at height
 * 1 lands, from the ONE light direction the lighting shader also uses. Move the light and the
 * shading, the shadow's direction and the shadow's LENGTH all move together.
 *
 * It is drawn into a MASK, not black over the picture. That is what lets the lighting pass
 * treat it as an absence of light -- so a shadow keeps the sky's colour in it instead of
 * being a hole -- and it is what lets the mask be blurred into a soft edge. The mask carries
 * the sprite's COVERAGE, which takes a shader (hd2d_shadow.frag): the fixed-function blender
 * can only give `sprite.rgb * a`, and a mask made of the fighter's own colours makes a shadow
 * that is darker under the bright parts of them.
 *
 * WHERE it goes comes from the game, not from a guess: the ellipse the game drew is at the
 * object's feet, so its centre and its bottom edge are the ground point. A sprite with no
 * preceding ellipse -- the background, the HUD, anything the object pass did not draw --
 * casts nothing.
 */
static void draw_cast_shadow(Tex *t, const SDL_FRect *src, const SDL_FRect *sprite,
                             const SDL_FRect *ground)
{
    /* THE PROJECTION, and it is the whole shadow. A point at height h above the ground lands
     * at h * (across, -up) on the screen, both from the one light vector. So:
     *
     *   the sprite's TOP, at its own height above its base, gives the far edge of the shadow
     *   -- which is what makes a low light throw a long one and an overhead light almost
     *   none. This used to be a fixed 0.30 of the sprite's height, so moving the light
     *   changed where a shadow pointed and never how long it was (issue #38).
     *
     *   the object's HEIGHT OFF THE FLOOR, when it is in the air, displaces the whole shadow
     *   by the same rule. The ellipse the game draws is at the object's feet ON THE FLOOR,
     *   but the sprite is lifted off it mid-jump; without this the shadow stayed welded under
     *   a jumping fighter however high they went.
     *
     * Both use the same two numbers, so the shading, the shadow's direction, its length and
     * its displacement in a jump cannot be given different lights.
     */
    float across = 0.0f, up = 0.0f;
    hd2d_shadow_project(&across, &up);

    const float cx = ground->x + ground->w * 0.5f;
    const float gy = ground->y + ground->h;          /* the ellipse's bottom edge */
    const float w  = sprite->w;

    const float top_dx = sprite->h * across;         /* where the sprite's head lands */
    const float top_dy = sprite->h * up;

    const float airborne = gy - (sprite->y + sprite->h);
    const float lift = airborne > 0.0f ? airborne : 0.0f;
    const float lift_dx = lift * across;
    const float lift_dy = lift * up;
    if (airborne > stat_airborne_max) {
        stat_airborne_max = airborne;
        if (getenv("LF2_SHADOW_DEBUG"))
            fprintf(stderr, "shadow: new highest lift %.0f px -- ground (%.0f,%.0f %.0fx%.0f), "
                            "sprite (%.0f,%.0f %.0fx%.0f)\n", (double)airborne,
                    (double)ground->x, (double)ground->y, (double)ground->w, (double)ground->h,
                    (double)sprite->x, (double)sprite->y, (double)sprite->w, (double)sprite->h);
    }

    const SDL_FColor c = { 1.0f, 1.0f, 1.0f, 1.0f };   /* the shader writes coverage */
    const float u0 = src->x / (float)t->w, u1 = (src->x + src->w) / (float)t->w;
    const float v0 = src->y / (float)t->h, v1 = (src->y + src->h) / (float)t->h;

    /* The sprite's foot edge sits at the ground point (displaced if it is in the air) and its
     * head edge lands where the projection puts it. */
    const float fx = cx + lift_dx, fy = gy - lift_dy;
    const float hx = fx + top_dx,  hy = fy - top_dy;

    const SDL_Vertex v[4] = {
        { { hx - w * 0.5f, hy }, c, { u0, v0 } },   /* sprite top-left     */
        { { hx + w * 0.5f, hy }, c, { u1, v0 } },   /* sprite top-right    */
        { { fx + w * 0.5f, fy }, c, { u1, v1 } },   /* sprite bottom-right */
        { { fx - w * 0.5f, fy }, c, { u0, v1 } },   /* sprite bottom-left  */
    };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    /* NONE, not BLEND: the shader alpha-tests, so overlapping shadows overwrite instead of
     * accumulating into a doubly dark patch where two fighters stand together. */
    SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_NONE);
    SDL_RenderGeometry(R, t->tex, v, 4, idx, 6);
}

/* HOW MUCH BIGGER THE OBJECTS ARE DRAWN THAN THE GAME DREW THEM.
 *
 * The frame is composed and drawn at the window's real pixels now, which is what makes it
 * sharp and gives a wide field of view -- but it also means a fighter is drawn at the ~40
 * rows the artist drew, and in a 1080-row window that is tiny. So the OBJECTS are scaled and
 * the world is not: the scenery keeps its native resolution and its full extent, and the
 * actors are drawn at the size a player expects.
 *
 * The factor is derived from real state rather than being a taste or an env var: it is how
 * many times the game's own 550-row screen fits in the window, rounded to a WHOLE number.
 * A whole number is not tidiness -- these are nearest-neighbour pixel-art sprites, and 1.96
 * would put some of their pixels down two screen pixels wide and others three. So the game's
 * own window gets 1x and is exactly what it always was, a 1080-row window gets 2x, and a
 * fighter comes out the same apparent size as before with the background twice as sharp and
 * twice as wide.
 */
static float object_scale(void)
{
    /* The WINDOW's height, not hostwin_height() -- that is the composition, which is pinned
     * at the game's 550 and would make this constantly 1. The rounding itself is
     * geom_object_scale, so runtime/test_geom.c exercises this rule and not a copy of it. */
    return (float)geom_object_scale(hw.win_h);
}

enum Pass { PASS_COLOUR, PASS_CHARS, PASS_SHADOW };

/* The three passes walk the SAME list in the SAME order through this one function, because
 * the alternative -- three loops kept in step by hand -- is how a character buffer ends up
 * describing a frame that was not the one drawn.
 *
 * PASS_COLOUR draws everything, exactly as the game composed it. The other two draw ONLY the
 * objects standing in the stage, which are the ones the game put a shadow ellipse in front
 * of: that is where the lighting applies and where the shadows come from, and it is why the
 * background, the HUD and the text are untouched by both.
 */
static void draw_list(List *l, int pass, float offx, float offy)
{
    const int skip = render_skip();
    const float scale = object_scale();
    int have_ground = 0;
    SDL_FRect ground = { 0, 0, 0, 0 };

    for (int i = 0; i < l->n; i++) {
        if (skip > 0 && (i % skip) == 0) continue;
        Entry *e = &l->e[i];
        SDL_FRect dst = e->dst;
        dst.x += offx;
        dst.y += offy;

        if (e->kind == E_GROUND) {
            /* THE FLOOR, AS THE GAME ITSELF DRAWS IT. Every ground marker is a point a
             * fighter is standing on, so the band these span IS the walkable floor on the
             * screen -- the game's own answer, and the cross-check any field read out of the
             * background record has to agree with before it can be called the z boundary
             * (issue #32). Collected in the colour pass only, so it counts each marker once. */
            if (pass == PASS_COLOUR) {
                const float gy = e->dst.y + e->dst.h;   /* the GAME's y, before placement */
                if (gy < ground_y_lo) ground_y_lo = gy;
                if (gy > ground_y_hi) ground_y_hi = gy;
            }
            /* The game's ellipse is REPLACED, not drawn under: leaving it would put a
             * checkerboard beneath every real shadow. With the light off it was never
             * recorded as a ground marker in the first place, and the game's own draw
             * stands. */
            ground = dst;
            have_ground = 1;
            if (pass == PASS_COLOUR) stat_ground++;
            continue;
        }

        /* The two lighting passes have nothing to say about anything that is not an object
         * in the field, and an object is precisely a sprite with a ground marker in front
         * of it. Everything else is skipped here rather than filtered later. */
        /* THE PAIRING HAS TO BE CHECKED, not assumed. The game draws an object's shadow
         * ellipse immediately before the object, so "the next sprite" is the right rule --
         * until a sprite is DROPPED between them. A sprite clipped entirely off the edge of
         * the composition arrives at Blt with an empty destination and never enters the list,
         * and the marker then binds to the NEXT object's sprite instead. Measured: a marker
         * at x 988 paired with a sprite at x 2076, a thousand pixels away and above the top
         * of the screen, which put a shadow under nothing and reported an object 874 px in
         * the air on a 550-row field.
         *
         * The test is the game's own geometry rather than a tolerance: the ellipse is drawn
         * AT the object's feet, so the object's sprite must overlap it horizontally. A
         * marker that fails it is discarded and the sprite is drawn as an ordinary one. */
        int is_object = have_ground && e->kind == E_TEX;
        if (is_object && (dst.x >= ground.x + ground.w || dst.x + dst.w <= ground.x)) {
            is_object = 0;
            stat_ground_orphan++;
        }
        have_ground = 0;
        if (pass != PASS_COLOUR && !is_object) continue;

        /* An object is drawn larger than the game drew it, about ITS OWN BASE: the sprite
         * grows where it stands, and its POSITION stays exactly the game's.
         *
         * The first version anchored this at the ground marker instead, so that a jump grew
         * by the same factor the fighter did. That is wrong, and measurably so: LF2 launches
         * objects a long way up, and doubling the height of one already 314 px above the
         * floor put it 628 px up and off the top of the screen. The world is drawn at 1:1
         * here; only the actors are magnified, so only their SIZE may change. A standing
         * object's base is its ground point anyway, which is why the two agree everywhere
         * except in mid-air -- the one place the difference matters.
         *
         * Every pass uses the rectangle computed here, so the picture, the character mask and
         * the shadow cannot disagree about where the object is or how big it is. */
        if (is_object && scale != 1.0f) {
            const float cx = dst.x + dst.w * 0.5f;
            const float base = dst.y + dst.h;
            dst.w *= scale;
            dst.h *= scale;
            dst.x = cx - dst.w * 0.5f;
            dst.y = base - dst.h;
        }

        if (e->kind == E_FILL) {
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(R, (uint8_t)(e->argb >> 16), (uint8_t)(e->argb >> 8),
                                   (uint8_t)e->argb, 255);
            SDL_RenderFillRect(R, &dst);
            stat_fill++;
            continue;
        }

        if (e->kind == E_TEX) {
            Tex *t = tex_for(e->src_pixels, e->sw, e->sh, e->spitch,
                             e->keyed, e->key_lo, e->key_hi);
            if (!t) continue;
            if (pass == PASS_SHADOW) {
                draw_cast_shadow(t, &e->src, &dst, &ground);
                stat_shadow++;
                continue;
            }
            if (pass == PASS_CHARS) {
                hd2d_chars_quad(ground.y + ground.h);
                SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_NONE);
                SDL_RenderTexture(R, t->tex, &e->src, &dst);
                continue;
            }
            SDL_SetTextureBlendMode(t->tex, e->keyed ? SDL_BLENDMODE_BLEND
                                                     : SDL_BLENDMODE_NONE);
            SDL_RenderTexture(R, t->tex, &e->src, &dst);
            stat_tex++;
            continue;
        }

        /* E_TILE, which only the colour pass reaches: GDI text is not in the field. */
        SDL_Texture *tt = tile_texture(e);
        if (!tt) continue;
        SDL_SetTextureBlendMode(tt, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_RenderTexture(R, tt, NULL, &dst);
        stat_tile++;
    }
}

/* ---- presenting ---- */

/* The cast shadows are part of the HD2D look, so they follow the same switch -- and they
 * follow whether the shaders actually loaded, because a mask nothing consumes would simply
 * delete the game's shadow and put nothing in its place. ddraw.c asks before it decides
 * whether the game's own ellipse becomes a ground marker or stays a picture. */
int render_shadows_enabled(void) { return hd2d_ready(); }

static SDL_Texture *rt_make(int w, int h, SDL_ScaleMode mode)
{
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) {
        SDL_SetTextureScaleMode(t, mode);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    }
    return t;
}

static void clear_to(SDL_Texture *rt, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderTarget(R, rt);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(R, r, g, b, a);
    SDL_RenderClear(R);
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
     * screen every game pixel becomes a 2x2 block and everything -- text, the lighting,
     * anything added later -- is quantised to the small grid before it is ever enlarged.
     *
     * Drawing from a display list removes that constraint: the quads carry the game's own
     * coordinates and the SCALE is applied as they are drawn, so the render target is the size
     * of the output and every later pass runs at full resolution. Sprites are pixel art and
     * still land on the same grid (NEAREST, above), but they are resampled ONCE instead of
     * twice, and the lighting is no longer working on an upscaled thumbnail.
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

    if (target && (target_w != ow || target_h != oh)) targets_free();
    if (!target) {
        target = rt_make(ow, oh, SDL_SCALEMODE_NEAREST);
        if (hd2d_ready()) {
            rt_albedo = rt_make(ow, oh, SDL_SCALEMODE_NEAREST);
            rt_chars   = rt_make(ow, oh, SDL_SCALEMODE_NEAREST);
            rt_shadow = rt_make(ow, oh, SDL_SCALEMODE_NEAREST);
        }
        if (!target || (hd2d_ready() && (!rt_albedo || !rt_chars || !rt_shadow))) {
            fprintf(stderr, "render: could not create the %dx%d render targets (%s) -- the "
                            "software compositor is presenting\n", ow, oh, SDL_GetError());
            targets_free();
            if (lp_mode != SDL_LOGICAL_PRESENTATION_DISABLED)
                SDL_SetRenderLogicalPresentation(R, lp_w, lp_h, lp_mode);
            stat_soft_frames++;
            return 0;
        }
        target_w = ow; target_h = oh;
    }
    /* NOTHING IS SCALED. The composition is the window's width and the game's own 550 rows
     * (runtime/ddraw.c hostwin_window_geometry says why the height cannot follow), so the
     * quads go down at 1:1 and are placed in the window rather than stretched to fill it.
     * The rows the game has no world for are black bands, above and below.
     *
     * `off` is the game's own widescreen centring, in the composition's coordinates;
     * ox/oy place the composition in the window. They are separate because they answer
     * different questions and a resize moves only the second. */
    float ox = 0.0f, oy = 0.0f;
    lf2_compose_placement(w, h, &ox, &oy);
    const int run_hd2d = hd2d_ready() && rt_albedo && rt_chars && rt_shadow;

    /* 1. THE PICTURE, exactly as the game composed it. With the light off this is the frame,
     *    and it is what tools/render_test.sh compares against the software compositor byte
     *    for byte. */
    clear_to(run_hd2d ? rt_albedo : target, 0, 0, 0, 255);
    draw_list(l, PASS_COLOUR, (float)off + ox, oy);

    if (run_hd2d) {
        /* 2. WHICH PIXELS ARE A FIGHTER. Cleared to zero: everything the game drew that is
         *    not an object standing in the field stays out of the mask, and the lighting
         *    leaves those pixels exactly as they are. */
        clear_to(rt_chars, 0, 0, 0, 255);
        if (hd2d_chars_begin(1.0f / (float)oh)) {
            draw_list(l, PASS_CHARS, (float)off + ox, oy);
            hd2d_chars_end();
        }

        /* 3. THE CAST SHADOWS, as a mask the light is taken away through. */
        clear_to(rt_shadow, 0, 0, 0, 255);
        if (hd2d_shadow_begin()) {
            draw_list(l, PASS_SHADOW, (float)off + ox, oy);
            hd2d_shadow_end();
        }

        /* Where the stage says its floor begins, in the window's rows. The lighting needs it
         * in the space it shades in, and this is the only place that knows both the game's
         * answer and where the composition was placed.
         *
         * GATED ON THE MATCH HUD, and that gate is not belt-and-braces. The background record
         * stays loaded after a fight, so the front end, the mode menu and character selection
         * would all be handed a perfectly valid floor band and would get their lower half
         * tinted -- a stage's geometry applied to a screen that has no stage in it. The HUD
         * strip is up exactly while the world is on screen, which is the same signal the
         * widescreen centring already switches on. */
        int zmin = 0, zmax = 0;
        const int have_floor = panel_hud_up() && bg_z_bounds(&zmin, &zmax);
        if (hd2d_post(rt_albedo, rt_chars, rt_shadow, target, ow, oh,
                      (float)zmin + oy, have_floor)) {
            stat_post++;
        } else {
            /* The pass could not run this frame. Show the picture rather than nothing --
             * and hd2d_report says why at the end of the run. */
            SDL_SetRenderTarget(R, target);
            SDL_SetTextureBlendMode(rt_albedo, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(R, rt_albedo, NULL, NULL);
        }
    }

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
        fprintf(stderr, "render: %dx%d of game drawn 1:1 into a %dx%d window at (%.0f,%.0f), "
                        "lighting %s\n", w, h, ow, oh, (double)ox, (double)oy,
                run_hd2d ? "on" : "OFF");
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
    for (int i = 0; i < nlists; i++) {
        for (int j = 0; j < lists[i].n; j++) lists[i].e[j].tile_tex = NULL;
        lists[i].n = 0;
    }
    /* The pooled textures are RELEASED, not destroyed -- see tile_texture. */
    for (int i = 0; i < tile_pool_n; i++) tile_pool[i].busy = 0;
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
    /* The allocation count is the number that matters: it must go FLAT once the pool is warm.
     * A count that keeps climbing with the frame count means tiles of ever-changing sizes and
     * the GPU allocator is being churned again, which is what wedged a GPU once. */
    fprintf(stderr, "render: %ld tile draws served by %d pooled textures, %ld allocations "
                    "(%.3f per frame -- this must be near zero once warm)%s\n",
            stat_tile, tile_pool_n, stat_tile_allocs,
            stat_frames ? (double)stat_tile_allocs / (double)stat_frames : 0.0,
            stat_tile_exhausted ? "" : "");
    if (stat_tile_exhausted)
        fprintf(stderr, "render: %ld tiles were NOT DRAWN -- the %d-texture pool was full, so "
                        "text is MISSING from those frames\n",
                stat_tile_exhausted, TILE_TEX_MAX);
    fprintf(stderr, "render: the light ran on %ld frame(s); %ld cast shadows from %ld ground "
                    "markers\n", stat_post, stat_shadow, stat_ground);
    if (stat_ground)
        fprintf(stderr, "render: the ground markers seen so far span y %.0f..%.0f in the "
                        "game's own coordinates -- that is the walkable floor, measured from "
                        "where the game put its shadows (issue #32)\n",
                (double)ground_y_lo, (double)ground_y_hi);
    /* The airborne term is invisible in a run where nobody leaves the floor, and "the shadow
     * never moved" and "nothing ever jumped" look identical on a screenshot. So the highest
     * lift seen is reported: a zero here means the offset was NEVER EXERCISED, not that it
     * does not work. */
    if (stat_ground_orphan)
        fprintf(stderr, "render: %ld ground markers were discarded because the sprite that "
                        "followed did not overlap them -- that object's own sprite was "
                        "dropped (clipped off the composition), so the marker had nothing to "
                        "belong to\n", stat_ground_orphan);
    if (stat_shadow)
        fprintf(stderr, "render: the highest an object got off its ground point was %.0f px%s\n",
                (double)stat_airborne_max,
                stat_airborne_max < 1.0f
                    ? " -- so NOTHING jumped in this run and the shadow's airborne offset was "
                      "never exercised"
                    : ", and its shadow was offset along the light by that much");
    else
        fprintf(stderr, "render: NO ground markers were seen, so the floor band is UNKNOWN -- "
                        "this run reached no match, or the stage's shadow object was never "
                        "identified\n");
    hd2d_report();
    if (hd2d_ready() && stat_ground && !stat_shadow)
        fprintf(stderr, "render: %ld ground markers but NO cast shadows -- every one was "
                        "followed by something that could not be drawn\n", stat_ground);
    if (hd2d_ready() && stat_frames && !stat_ground)
        fprintf(stderr, "render: NO ground markers were seen, so no shadow was replaced -- "
                        "the stage's shadow object was never identified\n");
    if (render_gpu_enabled() && stat_frames == 0)
        fprintf(stderr, "render: the GPU path presented NO frames -- every one fell back to "
                        "the software compositor, so these counters describe nothing\n");
    if (stat_dropped)
        fprintf(stderr, "render: %ld entries were DROPPED (list or tile arena full), so the "
                        "frames above are incomplete\n", stat_dropped);
}
