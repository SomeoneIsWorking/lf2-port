/* The native renderer -- see runtime/video/render.h for what it is and why it exists, and
 * runtime/video/hd2d.h for what is done with the geometry once it is here. */

#include "render.h"
#include "mesh.h"
#include "engine.h"
#include "texrect.h"
#include "framelife.h"
#include "overrides/geom.h"
#include "hd2d.h"
#include "options.h"
#include "guest.h"
#include "hostwin.h"
#include "rmlui.h"

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
enum { LIST_MAX = 8192, SURF_MAX = FL_LISTS_MAX, TEX_MAX = 512, TILE_BYTES_MAX = 4u << 20 };

enum EntryKind { E_TEX, E_FILL, E_TILE, E_GROUND, E_MESH };

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
    SDL_FRect tile_src;             /* the tw x th corner of it that this tile actually is */
    /* THE DRAW'S REAL DISTANCE, as a parallax depth -- 1.0 is the plane the fighters stand in,
     * larger is further away, and 0 means UNKNOWN (issue #63).
     *
     * NOT the same thing as the depth the engine's depth buffer holds, and conflating the two
     * is the trap. The buffer's depth is the draw's POSITION IN THE PAINTER ORDER, which is the
     * game's own answer about what covers what and must be preserved exactly. This is a
     * DISTANCE, and it is what a depth of field has to be a function of -- a blur driven off
     * draw order would be "defocus by how late something was drawn", which is the screen-space
     * effect issue #63 exists to not repeat.
     *
     * Known for the stage's background layers, whose depth is derivable from their own parallax
     * rate (C031), and for authored geometry, which carries it per vertex. Left 0 for the HUD,
     * the text and the port's own UI, which are not in the world at all. */
    float world_depth;
    /* E_MESH: hand-woven stage geometry, RECORDED rather than pre-rendered.
     *
     * It used to be a finished SDL_Texture, which meant the background override had to run a
     * whole render pass per parallax gap before the list was even drawn -- the arrangement
     * issue #64 exists to remove. A display list records; it does not draw. The vertices belong
     * to the stage and are stable for as long as it is loaded, so this is a reference and not a
     * copy.
     *
     * `slot` is only for the old path, which still has to composite each gap as its own
     * texture because SDL_Render cannot take geometry at all. */
    const void *mesh_v;             /* MeshVertex *, owned by the background override */
    int         mesh_n, mesh_slot;
    int         mesh_camera, mesh_view_w, mesh_view_h;
} Entry;

/* The Entry array and its destination. HOW MANY entries it holds, where the port's overlay
 * starts in it, and everything else about the frame's lifetime live in FrameLife (framelife.h)
 * so that they can be tested without a GPU -- FL.n[i] belongs to lists[i]. */
typedef struct {
    uint32_t dst_pixels;
    Entry   *e;
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
static FrameLife FL;
static List   lists[SURF_MAX];
static Tex    texes[TEX_MAX];
static int    ntexes;

static void texture_uv(const SDL_FRect *src, int sw, int sh,
                       float *u0, float *v0, float *u1, float *v1)
{
    const int whole = src->x == 0.0f && src->y == 0.0f
                   && src->w == (float)sw && src->h == (float)sh;
    if (getenv("LF2_TEXRECT_EDGE") || whole) {
        *u0 = src->x / (float)sw; *v0 = src->y / (float)sh;
        *u1 = (src->x + src->w) / (float)sw;
        *v1 = (src->y + src->h) / (float)sh;
    } else {
        texrect_centres(src->x, src->y, src->w, src->h, sw, sh, u0, v0, u1, v1);
    }
}

static uint8_t *tile_arena;

/* Tile textures are POOLED and reused across frames rather than allocated per frame; see
 * tile_texture below for the GPU reset that made that necessary. */
/* The pool's SIZES and which are claimed live in FrameLife; this is only the textures, at the
 * same indices. The bound is tiles per frame, not distinct sizes -- see framelife.h. */
enum { TILE_TEX_MAX = FL_POOL_MAX };
static SDL_Texture *tile_tex_pool[TILE_TEX_MAX];
static long    stat_tile_allocs;
static int      tile_list = -1;     /* the list index the open tile belongs to */
static int      tile_index;

static SDL_Texture *target;         /* the frame the player is shown */
static int          target_w, target_h;

static long stat_frames, stat_tex, stat_fill, stat_tile, stat_uploads, stat_dropped;
static long stat_mesh;   /* geometry passes placed in the list -- see render_report */
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
    fl_init(&FL);
    if (!render_gpu_enabled()) return;
    if (!tile_arena) tile_arena = malloc(TILE_BYTES_MAX);
    /* The depth-tested geometry pass shares this renderer's device (issues #49, #62). It
     * reports why it cannot run rather than going quiet, because a pass that draws nothing is
     * indistinguishable from a stage with no geometry authored for it. */
    mesh_init(r);
    engine_init(r);
    /* The settings screen is RmlUi over the same renderer (issue #70). It fails loudly when
     * it cannot, and the pause menu refuses SETTINGS on the software path. */
    if (!rmlui_init(r, hw.window))
        fprintf(stderr, "render: the RmlUi settings screen is not available\n");
}

static void targets_free(void)
{
    if (target) { SDL_DestroyTexture(target); target = NULL; }
    target_w = target_h = 0;
}

void render_shutdown(void)
{
    for (int i = 0; i < ntexes; i++)
        if (texes[i].tex) { SDL_DestroyTexture(texes[i].tex); texes[i].tex = NULL; }
    ntexes = 0;
    for (int i = 0; i < FL.pool_n; i++)
        if (tile_tex_pool[i]) { SDL_DestroyTexture(tile_tex_pool[i]); tile_tex_pool[i] = NULL; }
    targets_free();
    for (int i = 0; i < FL.nlists; i++) { free(lists[i].e); lists[i].e = NULL; }
    fl_init(&FL);
    rmlui_shutdown();
    free(tile_arena); tile_arena = NULL;
}

/* The list index for a destination surface, or -1. An index rather than a pointer because
 * FrameLife holds the other half of every list at the same index. */
static int list_index(uint32_t dst_pixels)
{
    for (int i = 0; i < FL.nlists; i++)
        if (lists[i].dst_pixels == dst_pixels) return i;
    const int i = fl_list_add(&FL);
    if (i < 0) return -1;
    lists[i].dst_pixels = dst_pixels;
    if (!lists[i].e) lists[i].e = calloc(LIST_MAX, sizeof *lists[i].e);
    return lists[i].e ? i : -1;
}

/* The retained frame is cleared by the first call that records over it, not by the present
 * that finished it -- see render_frame_reset. fl_touch decides; this drops the SDL textures
 * that went with the frame it cleared. */
static void frame_touch(void)
{
    if (!FL.spent) return;
    /* The lengths BEFORE the clear, because those entries are the ones holding pooled
     * textures that are about to be released. */
    int was[FL_LISTS_MAX];
    const int nl = FL.nlists;
    for (int i = 0; i < nl; i++) was[i] = FL.n[i];
    fl_touch(&FL);
    for (int i = 0; i < nl; i++)
        for (int j = 0; j < was[i]; j++) lists[i].e[j].tile_tex = NULL;
    tile_list = -1;
}

/* The depth of the draw about to be recorded, in the same units as Entry.world_depth. Set by
 * the background override around a layer's draw, the way world_band_hint marks a world band --
 * the game's own draw call goes through the guest, so there is no argument to add. */
static float depth_hint;
void render_depth_hint_set(float d) { depth_hint = d > 0.0f ? d : 0.0f; }

static Entry *entry_push(uint32_t dst_pixels)
{
    frame_touch();
    const int li = list_index(dst_pixels);
    if (li < 0) return NULL;
    if (FL.n[li] >= LIST_MAX) { lists[li].dropped++; stat_dropped++; return NULL; }
    Entry *e = &lists[li].e[FL.n[li]++];
    memset(e, 0, sizeof *e);
    e->world_depth = depth_hint;
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

/* A finished geometry pass, placed at THIS point in the painter order (issue #62).
 *
 * WHY IT IS AN ENTRY RATHER THAN A DRAW AT A FIXED MOMENT. The whole reason the background
 * override calls this between layers is that a hand-woven set spans parallax depths and the
 * game paints its own layers between them -- so WHERE in the list the quad lands is the entire
 * content of the call. Drawing it "before the layers" or "after them" from somewhere else in
 * this file would throw that away.
 *
 * The texture is mesh.c's and is not owned, not pooled and not uploaded: it is the same GPU
 * object the pass rendered into (claim C030), and it stays live until that slot is drawn
 * again. That is why the caller must use a different slot per insertion point.
 */
void render_stage_mesh(uint32_t dst_pixels, const void *verts, int n, int slot,
                       int camera, int view_w, int view_h)
{
    if (!render_gpu_enabled() || !verts || n <= 0 || view_w <= 0 || view_h <= 0) return;
    Entry *e = entry_push(dst_pixels);
    if (!e) return;
    e->kind = E_MESH;
    e->dst = (SDL_FRect){ 0.0f, 0.0f, (float)view_w, (float)view_h };
    e->mesh_v = verts;
    e->mesh_n = n;
    e->mesh_slot = slot;
    e->mesh_camera = camera;
    e->mesh_view_w = view_w;
    e->mesh_view_h = view_h;
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

/* THE TILE'S PIXELS AND ITS PLACE ON SCREEN ARE TWO DIFFERENT SIZES (issue #45).
 *
 * `x, y, w, h` are where the tile goes, in the GAME's own coordinates -- that is the game's
 * layout and it never changes. `tw, th` are how many pixels the caller actually rasterised.
 * They used to be the same number, which is precisely why text did not get sharper as the
 * window grew: a glyph rasterised into an 8x16 tile is 8x16 texels however large the
 * destination is, and issue #41 then scaled that tile up with every other quad.
 *
 * Give it more texels for the same destination and the tile is drawn at the window's real
 * resolution: draw_list scales the DESTINATION by the world scale, the texture is stretched
 * to fit it, and a caller that rasterised at exactly destination*scale lands 1:1 on screen
 * pixels. Nothing here has to know about the scale -- it only has to stop assuming the two
 * sizes are equal. */
uint32_t *render_tile_begin(uint32_t dst_pixels, int x, int y, int w, int h, int tw, int th)
{
    if (tw <= 0) tw = w;
    if (th <= 0) th = h;
    if (!render_gpu_enabled() || !tile_arena || w <= 0 || h <= 0 || tw <= 0 || th <= 0)
        return NULL;
    frame_touch();
    const uint32_t need = (uint32_t)tw * (uint32_t)th * 4u;
    if (need > TILE_BYTES_MAX - FL.tile_used) { stat_dropped++; return NULL; }
    const int li = list_index(dst_pixels);
    if (li < 0 || FL.n[li] >= LIST_MAX) {
        if (li >= 0) lists[li].dropped++;
        stat_dropped++;
        return NULL;
    }
    Entry *e = &lists[li].e[FL.n[li]];
    memset(e, 0, sizeof *e);
    e->kind = E_TILE;
    e->dst = (SDL_FRect){ (float)x, (float)y, (float)w, (float)h };
    e->tile_off = FL.tile_used;
    e->tw = tw; e->th = th;
    uint32_t *p = (uint32_t *)(tile_arena + FL.tile_used);
    memset(p, 0, need);
    FL.tile_used += need;
    tile_list = li; tile_index = FL.n[li];
    return p;
}

void render_tile_end(void)
{
    if (tile_list < 0) return;
    FL.n[tile_list] = tile_index + 1;   /* commit the entry only now */
    tile_list = -1;
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
/* A POOLED TEXTURE MAY BE BIGGER THAN THE TILE IT SERVES, and the tile uses its top-left
 * corner. That is what bounds the pool.
 *
 * Keying on the EXACT size, which is what this did first, rests on the premise in the comment
 * above -- "a tile's size repeats constantly". That is true of a glyph, which is one 8x16 cell
 * scaled, and FALSE of the other caller: h_TextOutA rasterises a whole string as one tile, so
 * its width is the width of that string, and a run meets as many sizes as it meets distinct
 * strings. The pool grew one entry per string width until it hit its 128 cap and then dropped
 * tiles -- text silently missing from the frame, on a run long enough to get there.
 *
 * Rounding the ALLOCATION up to a 32-pixel grid, and reusing any free texture at least as
 * large, replaces "one entry per distinct size" with "one entry per distinct bucket". The
 * upload writes tw x th into the corner and the draw takes that corner as its source rect, so
 * nothing outside it is ever sampled and no tile is stretched. */
static SDL_Texture *tile_texture(Entry *e)
{
    if (e->tile_tex) return e->tile_tex;

    int slot = fl_pool_claim(&FL, e->tw, e->th);
    if (slot < 0) {
        slot = fl_pool_add(&FL, e->tw, e->th);
        if (slot < 0) return NULL;          /* fl_pool_add counted the exhaustion */
        SDL_Texture *made = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              FL.pool_w[slot], FL.pool_h[slot]);
        if (!made) { FL.pool_exhausted++; FL.pool_n--; return NULL; }
        SDL_SetTextureScaleMode(made, SDL_SCALEMODE_NEAREST);
        /* PREMULTIPLIED: the writer already multiplied the colour by its coverage, so the
         * blend is src + dst*(1-a) rather than src*a + dst*(1-a). Using plain BLEND here
         * would darken every glyph edge. */
        SDL_SetTextureBlendMode(made, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        tile_tex_pool[slot] = made;
        stat_tile_allocs++;
    }
    SDL_Texture *t = tile_tex_pool[slot];

    /* Only the tw x th corner is written, and only it is ever read -- see tile_src below. The
     * rest of the texture holds whatever the last tile to use it left there. */
    SDL_Rect corner = { 0, 0, e->tw, e->th };
    void *px = NULL; int dp = 0;
    if (SDL_LockTexture(t, &corner, &px, &dp)) {
        const uint32_t *src = (const uint32_t *)(tile_arena + e->tile_off);
        for (int y = 0; y < e->th; y++)
            memcpy((uint8_t *)px + (size_t)y * (size_t)dp, src + (size_t)y * (size_t)e->tw,
                   (size_t)e->tw * 4);
        SDL_UnlockTexture(t);
    }
    e->tile_src = (SDL_FRect){ 0.0f, 0.0f, (float)e->tw, (float)e->th };
    e->tile_tex = t;
    return t;
}

/* LF2_RENDER_SKIP=<n> drops every nth entry. It is the negative arm of
 * tools/routes/render_test.sh: "the GPU frame matches the software frame" would pass just as
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

/* The cast-shadow machinery that lived here moved into the engine with the lighting
 * (engine.c's lighting_passes): the shadow is a mask the light pass reads, which is the whole
 * reason it exists, and the engine is the thing that has the mask. render_shadows_enabled()
 * is what ddraw.c asks. */

/* SDL_RenderTexture accepts a pixel-space source rectangle, but the GPU backend normalizes
 * its integer edges onto boundaries shared by adjacent sheet cells. Draw the quad explicitly
 * so the old renderer and the engine use the same texel-centre contract. */
static void draw_texture_quad(Tex *t, const SDL_FRect *src, const SDL_FRect *dst)
{
    /* Restore the complete old run.sh path, not merely its UV values. SDL_RenderTexture
     * constructs its own quad internally, so an injector that kept this function and only
     * selected edge UVs did not actually reproduce the reported renderer. */
    if (getenv("LF2_TEXRECT_EDGE")) {
        SDL_RenderTexture(R, t->tex, src, dst);
        return;
    }
    float u0, v0, u1, v1;
    texture_uv(src, t->w, t->h, &u0, &v0, &u1, &v1);
    const SDL_FColor white = { 1.0f, 1.0f, 1.0f, 1.0f };
    const SDL_Vertex v[4] = {
        { { dst->x,          dst->y },          white, { u0, v0 } },
        { { dst->x + dst->w, dst->y },          white, { u1, v0 } },
        { { dst->x + dst->w, dst->y + dst->h }, white, { u1, v1 } },
        { { dst->x,          dst->y + dst->h }, white, { u0, v1 } },
    };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
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
/* HOW BIG A GAME PIXEL IS DRAWN, and it is now the same number for everything (issue #41).
 *
 * This used to magnify only the OBJECTS, by a whole number, leaving the stage at 1:1 -- so a
 * 1080-row window put a 2x fighter on a 1x floor and the picture disagreed with itself about
 * how big a pixel was. The scale is the world's, taken from the window, and every quad in the
 * list gets it: the stage, the actors, the HUD and the text all grow together, which is the
 * only way the frame stays one picture.
 *
 * It is applied HERE, per quad, and not to the finished frame. That is the distinction issue
 * #41 turns on: the destination rectangles are floats and the target is the size of the
 * window, so the geometry is exact and only a sprite's own texels are resampled -- once,
 * NEAREST, from the source art. Scaling the composed frame instead is what the port used to
 * do and it quantises everything to the small grid first. */
static float world_scale(void)
{
    return lf2_world_scale();
}

/* The ONE pass: everything the game composed, in the order it composed it. The two lighting
 * passes that shared this function -- the character buffer and the cast-shadow mask -- moved
 * into the engine with the lighting itself (engine.c's lighting_passes), so this is the plain
 * picture only, and it is what the engine path renders against for byte identity.
 *
 * The ground-marker pairing that lived here is gone for the same reason: a shadow ellipse is
 * recorded as a MARKER only when the engine is drawing (render_shadows_enabled), and when the
 * engine draws, this function is not the thing that draws. A marker can still reach it in one
 * corner -- the engine's frame failing after the list was recorded -- and it is skipped there
 * exactly as it was when the marker stood for a shadow that was then going to be drawn. */
/* `world` is the widescreen centring offset, in the COMPOSITION's own pixels, so it is added
 * before the scale. `ox`/`oy` place the scaled picture in the window and are in the window's
 * pixels, so they are added after. Getting that order wrong scales the centring too and the
 * world slides sideways as the window grows -- which is why they are separate parameters
 * rather than one pre-summed offset. */
static void draw_list(List *l, float world, float ox, float oy, int from, int to)
{
    const int skip = render_skip();
    const float scale = world_scale();

    for (int i = from; i < to; i++) {
        if (skip > 0 && (i % skip) == 0) continue;
        Entry *e = &l->e[i];
        SDL_FRect dst;
        dst.x = (e->dst.x + world) * scale + ox;
        dst.y = e->dst.y * scale + oy;
        dst.w = e->dst.w * scale;
        dst.h = e->dst.h * scale;

        if (e->kind == E_GROUND) continue;   /* a marker, never a picture -- see the note above */

        /* THERE IS NO SEPARATE OBJECT MAGNIFICATION ANY MORE, and removing it was the point of
         * issue #41. Objects used to be scaled here, about their own base, by a whole-number
         * factor while the world stayed at 1:1 -- a 2x fighter standing on a 1x floor. Every
         * quad is scaled uniformly at the top of this loop now, so an object's size and its
         * position come from the same number and a jump lands where the stage says it does.
         *
         * The trap that cost a rewrite the first time, kept because the same shape of mistake
         * is available in the uniform scale: anchoring a size change anywhere but the sprite's
         * own rectangle. Scaling an object about the GROUND MARKER grew the height of its jump
         * by the same factor it grew the fighter, and LF2 launches objects a long way up --
         * one already 314 px above the floor went to 628 and off the top of the screen. A
         * uniform scale of the whole frame cannot do that, because the floor moves with it. */

        if (e->kind == E_FILL) {
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(R, (uint8_t)(e->argb >> 16), (uint8_t)(e->argb >> 8),
                                   (uint8_t)e->argb, 255);
            SDL_RenderFillRect(R, &dst);
            stat_fill++;
            continue;
        }

        if (e->kind == E_MESH) {
            /* Blended, because the pass clears to TRANSPARENT: every texel its geometry does
             * not cover has to let the game's own layers through. NONE would put a rectangle
             * over the stage.
             *
             * Not lit by hd2d: the geometry is shaded in the pass's own fragment shader, from
             * the same key light and the same vector, so a solid's shading and a fighter's
             * shadow cannot disagree. Running it through the sprite lighting as well would
             * light it twice. */
            /* THE OLD PATH still has to render the geometry into its own target and then
             * composite that target as a texture, because SDL_Render cannot take geometry with
             * a depth buffer at all. That is the cost issue #64 is about, and it is left intact
             * here so the two paths stay diffable. */
            SDL_Texture *mt = mesh_draw(e->mesh_slot, (const MeshVertex *)e->mesh_v, e->mesh_n,
                                        e->mesh_view_w, e->mesh_view_h,
                                        e->mesh_camera, e->mesh_view_w, e->mesh_view_h, NULL);
            if (!mt) continue;
            SDL_SetTextureBlendMode(mt, SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(R, mt, NULL, &dst);
            stat_mesh++;
            continue;
        }

        if (e->kind == E_TEX) {
            Tex *t = tex_for(e->src_pixels, e->sw, e->sh, e->spitch,
                             e->keyed, e->key_lo, e->key_hi);
            if (!t) continue;
            SDL_SetTextureBlendMode(t->tex, e->keyed ? SDL_BLENDMODE_BLEND
                                                     : SDL_BLENDMODE_NONE);
            draw_texture_quad(t, &e->src, &dst);
            stat_tex++;
            continue;
        }

        /* E_TILE, which only the colour pass reaches: GDI text is not in the field. */
        SDL_Texture *tt = tile_texture(e);
        if (!tt) continue;
        SDL_SetTextureBlendMode(tt, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_RenderTexture(R, tt, &e->tile_src, &dst);
        stat_tile++;
        fl_tile_drawn(&FL);
    }
}

/* ---- presenting ---- */

/* Are the cast shadows part of the picture? They are part of the HD2D look, so they follow
 * the lighting's switch -- and they follow whether the engine is actually drawing, because a
 * mask nothing consumes would simply delete the game's shadow and put nothing in its place.
 * ddraw.c asks before it decides whether the game's own ellipse becomes a ground marker or
 * stays a picture. */
int render_shadows_enabled(void)
{
    return engine_enabled() && opt_lighting();
}

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


/* ---- the engine's colour pass (issue #64) --------------------------------------------------
 *
 * The list, turned into quads and handed to the port's own engine instead of to SDL_Render.
 * Returns 0 when the engine is not the thing drawing, and the caller then runs draw_list as it
 * always has -- which is what keeps the old path selectable as the A/B control arm.
 *
 * IT MUST PRODUCE THE SAME PICTURE, and that is the whole of this first version. The engine
 * exists to light the scene with a real depth buffer, but a first version that both replaced
 * the renderer AND changed the shading would fail tools/e2e.sh render for two reasons at once
 * and could not be told apart from a broken one. So this is a TRANSLATION of draw_list's colour
 * pass, entry kind for entry kind, and nothing more.
 *
 * The one thing it does that draw_list cannot: every quad lands in a real D32_FLOAT depth
 * buffer, ordered by its position in the list. The picture is unchanged; the depth is new, and
 * it is what the lighting step needs next.
 */
static EngineQuad *eq;
static int         eq_cap;
static EngineGeom  egeom[64];
static long        stat_engine_frames, stat_engine_geom;

static EngineQuad *eq_reserve(int n)
{
    if (n <= eq_cap) return eq;
    EngineQuad *p = realloc(eq, (size_t)n * sizeof *p);
    if (!p) return NULL;
    eq = p; eq_cap = n;
    return eq;
}

static int engine_colour_pass(List *l, int li, int ov, float world, float ox, float oy,
                              float scale, int ow, int oh, SDL_Texture *dst)
{
    (void)li;
    if (!engine_enabled()) return 0;

    EngineQuad *out = eq_reserve(ov > 0 ? ov : 1);
    if (!out) return 0;

    const int skip = render_skip();
    int n = 0, ng = 0;
    /* THE GROUND PAIRING, exactly as draw_list does it (C019): the game draws an object's shadow
     * ellipse immediately before the object, so a sprite preceded by a marker is an object
     * STANDING IN THE STAGE rather than a HUD element or a piece of text.
     *
     * The engine needs it for a different reason than the lighting does. An object is part of the
     * WORLD, and it stands at the fighters' plane -- parallax depth 1.0, which is not a guess but
     * the definition of that plane (C018/C031: every object shifts with the camera at rate 1).
     * Writing that into the G-buffer is what lets a later effect ask "is this pixel part of the
     * scene" and get a truthful answer for a fighter as well as for a layer. */
    int have_ground = 0;
    SDL_FRect ground = { 0, 0, 0, 0 };
    for (int i = 0; i < ov; i++) {
        if (skip > 0 && (i % skip) == 0) continue;   /* the negative arm, honoured identically */
        Entry *e = &l->e[i];
        if (e->kind == E_GROUND) {
            ground.x = (e->dst.x + world) * scale + ox;
            ground.y = e->dst.y * scale + oy;
            ground.w = e->dst.w * scale;
            ground.h = e->dst.h * scale;
            have_ground = 1;
            /* COUNTED HERE TOO, and that is not bookkeeping. This function REPLACES the
             * PASS_COLOUR walk, which is where draw_list counts markers -- so with the engine
             * selected the counters stayed at zero while shadows were being drawn, and
             * render_report then printed "NO ground markers were seen, so no shadow was
             * replaced". That is a diagnostic asserting a negative its own method could not
             * have contradicted, on the path this port is moving to. */
            stat_ground++;
            /* THE FLOOR, AS THE GAME ITSELF DRAWS IT, measured here too so render_report's
             * band survives the move off PASS_COLOUR. The game's y is used, exactly as
             * draw_list recorded it, so the span stays in the coordinates the report claims. */
            const float gy = e->dst.y + e->dst.h;
            if (gy < ground_y_lo) ground_y_lo = gy;
            if (gy > ground_y_hi) ground_y_hi = gy;
            continue;
        }
        if (e->kind == E_MESH) {
            /* The marker binds to the next SPRITE, so a mesh between the two must not consume
             * it. draw_list clears `have_ground` for every kind at one place; here the two
             * early returns meant this one did not. Unreachable today -- geometry is submitted
             * per layer gap, never between a marker and its object -- and it is one rule
             * written in two places, which is how the two drift. */
            have_ground = 0;
            /* INTO THE SAME PASS, at this point in the painter order -- not composited back as
             * a texture. `at` is the quad index it precedes, so the engine can give it the
             * sliver of depth between this quad and the last. Recording where it goes is all
             * this loop does; the engine does the drawing. */
            if (ng < (int)(sizeof egeom / sizeof egeom[0]) && e->mesh_v && e->mesh_n > 0) {
                egeom[ng].v = e->mesh_v;
                egeom[ng].n = e->mesh_n;
                egeom[ng].at = n;
                egeom[ng].camera = e->mesh_camera;
                /* The SAME map the quads get, written as scale and bias: a stage pixel sx
                 * lands at (sx + world) * scale + ox in the output, and that in clip space. */
                egeom[ng].sx_scale = 2.0f * scale / (float)ow;
                egeom[ng].sx_bias  = 2.0f * (world * scale + ox) / (float)ow - 1.0f;
                egeom[ng].sy_scale = -2.0f * scale / (float)oh;
                egeom[ng].sy_bias  = 1.0f - 2.0f * oy / (float)oh;
                ng++;
                stat_engine_geom++;
            }
            continue;
        }

        /* The marker binds to the NEXT sprite, and the pairing is CHECKED rather than assumed:
         * a sprite clipped entirely off the composition never enters the list, and the marker
         * would then bind to the next object's sprite instead -- measured once at a thousand
         * pixels away. The test is the game's own geometry: the ellipse is at the object's feet,
         * so the sprite must overlap it horizontally. */
        const int was_ground = have_ground;
        have_ground = 0;

        EngineQuad *q = &out[n];
        memset(q, 0, sizeof *q);
        q->x = (e->dst.x + world) * scale + ox;
        q->y = e->dst.y * scale + oy;
        q->w = e->dst.w * scale;
        q->h = e->dst.h * scale;
        q->r = q->g = q->b = q->a = 1.0f;
        q->world_depth = e->world_depth;
        if (was_ground && e->kind == E_TEX) {
            const float qx = (e->dst.x + world) * scale + ox, qw = e->dst.w * scale;
            if (qx < ground.x + ground.w && qx + qw > ground.x) {
                q->world_depth = 1.0f;      /* an object, standing in the fighters' plane */
                /* MARKED FOR THE LIGHTING CHAIN, with the ground it stands on: the engine
                 * draws the object's own quad into the character mask and its sheared
                 * silhouette into the cast-shadow mask. The shadow is anchored to the GROUND
                 * ELLIPSE's centre (issue #72) -- the ellipse is the game's own answer for
                 * where the character's feet are -- not to the sprite quad's centre, which
                 * carries a per-frame offset from the feet (measured 9 px on one frame). */
                q->is_object = 1;
                q->ground_gy = ground.y + ground.h;
                q->ground_cx = ground.x + ground.w * 0.5f;
                /* One cast shadow per object -- the count draw_list's PASS_SHADOW used to
                 * keep, moved here with the shadow itself. */
                stat_shadow++;
                /* The airborne term, measured here so render_report's "highest lift" keeps a
                 * writer after draw_cast_shadow left. Same numbers the engine's shadow quad
                 * uses -- how far the object's own base sits above the floor. */
                const float air = q->ground_gy - (q->y + q->h);
                if (air > stat_airborne_max) stat_airborne_max = air;
            } else
                stat_ground_orphan++;       /* the same failed pairing draw_list counts */
        }

        if (e->kind == E_FILL) {
            q->r = (float)((e->argb >> 16) & 0xff) / 255.0f;
            q->g = (float)((e->argb >> 8) & 0xff) / 255.0f;
            q->b = (float)(e->argb & 0xff) / 255.0f;
            q->blend = 0;                       /* BLEND_NONE, as SDL_BLENDMODE_NONE was */
            n++;
            continue;
        }
        if (e->kind == E_TILE) {
            if (!tile_arena) continue;
            q->host_argb  = tile_arena + e->tile_off;
            q->host_w     = e->tw;
            q->host_h     = e->th;
            q->host_pitch = e->tw * 4;
            /* The whole tile, not a corner: the engine keys its cache on the tile's own size,
             * so there is no pooled texture with a stale remainder to avoid reading. That
             * corner rectangle was a consequence of the pool, not of the tile. */
            q->u0 = 0.0f; q->v0 = 0.0f; q->u1 = 1.0f; q->v1 = 1.0f;
            q->blend = 2;                       /* premultiplied */
            n++;
            continue;
        }
        /* E_TEX */
        if (e->sw <= 0 || e->sh <= 0) continue;
        q->src_pixels = e->src_pixels;
        q->sw = e->sw; q->sh = e->sh; q->spitch = e->spitch;
        q->keyed = e->keyed; q->key_lo = e->key_lo; q->key_hi = e->key_hi;
        /* LF2_TEXRECT_EDGE restores the old shared-boundary coordinates as a defect
         * injector. At 1x they often happen to select the right texel; magnification is the
         * discriminator, where it produces issue #67's green line and issue #68's adjacent
         * Bandit eye. */
        texture_uv(&e->src, e->sw, e->sh, &q->u0, &q->v0, &q->u1, &q->v1);
        q->blend = e->keyed ? 1 : 0;
        n++;
    }

    /* WHERE THE STAGE SAYS ITS FLOOR BEGINS, in the output's rows. The lighting pass needs it
     * in the space it shades in, and this is the only place that knows both the game's answer
     * and where the composition was placed.
     *
     * GATED ON THE MATCH HUD, and that gate is not belt-and-braces. The background record
     * stays loaded after a fight, so the front end, the mode menu and character selection
     * would all be handed a perfectly valid floor band and would get their lower half tinted
     * -- a stage's geometry applied to a screen that has no stage in it. The HUD strip is up
     * exactly while the world is on screen, which is the same signal the widescreen centring
     * already switches on. */
    int zmin = 0, zmax = 0;
    const int have_floor = panel_hud_up() && bg_z_bounds(&zmin, &zmax);
    SDL_Texture *frame = engine_draw(out, n, egeom, ng, ow, oh,
                                     (float)zmin * scale + oy, have_floor);
    if (!frame) return 0;                        /* engine_draw said why; fall back */

    /* The engine's target, placed on the caller's. It is the SAME GPU object the pass rendered
     * into (C030), so this is a blit and not a readback -- and the blit is where the DEFOCUS
     * happens, since it is the one moment the finished frame and its G-buffer are both to hand.
     *
     * The radius is in texels of the OUTPUT, so it scales with the window: a fixed pixel radius
     * would be a heavy blur at 794x550 and a hairline at 4K, which is a blur that is a function
     * of resolution rather than of distance. */
    const float dof_radius = (float)oh / 110.0f;   /* 5 texels at the game's own 550 rows */
    if (!engine_present(dst, dof_radius)) {
        SDL_SetRenderTarget(R, dst);
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(R, frame, NULL, NULL);
    }
    if (opt_lighting()) stat_post++;     /* "the light ran" -- the engine's own count is engine_report's */
    stat_engine_frames++;
    return 1;
}

int render_present(uint32_t src_pixels, int off, int w, int h)
{
    if (!render_gpu_enabled() || !R) return 0;
    int li = -1;
    for (int i = 0; i < FL.nlists; i++) if (lists[i].dst_pixels == src_pixels) li = i;
    List *l = li >= 0 ? &lists[li] : NULL;
    /* No list for this surface means the frame was not built through the routes this
     * renderer captures. Say so once and hand the frame back to the software path rather
     * than presenting an empty screen -- a black frame is the one failure mode that looks
     * like a crash and tells nobody why. */
    if (!l || FL.n[li] == 0) {
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
        if (!target) {
            fprintf(stderr, "render: could not create the %dx%d render target (%s) -- the "
                            "software compositor is presenting\n", ow, oh, SDL_GetError());
            targets_free();
            if (lp_mode != SDL_LOGICAL_PRESENTATION_DISABLED)
                SDL_SetRenderLogicalPresentation(R, lp_w, lp_h, lp_mode);
            stat_soft_frames++;
            return 0;
        }
        target_w = ow; target_h = oh;
    }
    /* WHERE THE PICTURE GOES AND HOW BIG IT IS DRAWN (issue #41). The composition is `w` x `h`
     * game pixels; geom_compose_rect says what rectangle of the window that fills, and every
     * quad below is scaled into it as it is drawn.
     *
     * `off` is the game's own widescreen centring, in the COMPOSITION's coordinates, so it is
     * added before the scale; place.x/y are the window's, so they are added after. They stay
     * separate all the way down into draw_list for that reason. */
    SDL_FRect place;
    lf2_compose_rect(w, h, &place);
    const float ox = place.x, oy = place.y;
    const float scale = lf2_world_scale();

    /* 1. THE PICTURE, exactly as the game composed it -- then, inside the engine, the
     *    lighting chain: the character mask, the cast-shadow mask and the light pass run over
     *    this same frame before it is presented (engine.c's lighting_passes). With the light
     *    off the engine returns the unlit picture and the frame IS what the game drew, which
     *    is what tools/routes/render_test.sh compares against the software compositor byte for
     *    byte. */
    /* WHERE THE GAME'S PICTURE ENDS AND THE PORT'S OWN UI BEGINS. The controls hint and the
     * pause menu are recorded into this same list -- that is what lets the renderer present
     * the frames they sit on at all (issue #52) -- but they are not part of the scene and
     * must not be lit. The engine marks objects from the ground-pairing in the FIRST part of
     * the list only, so a fighter is never re-composited over the menu that is supposed to be
     * frozen and dimmed. */
    const int ov = fl_overlay_at(&FL, li);
    const int ln = FL.n[li];

    clear_to(target, 0, 0, 0, 255);
    if (!engine_colour_pass(l, li, ov, (float)off, ox, oy, scale, ow, oh, target))
        draw_list(l, (float)off, ox, oy, 0, ov);

    /* The port's UI, over the finished picture and after the light. */
    if (ov < ln) {
        SDL_SetRenderTarget(R, target);
        draw_list(l, (float)off, ox, oy, ov, ln);
    }

    /* The RmlUi settings screen, over the same frame (issue #70): the document composites
     * into the composed frame between the game's draw and the present, so it is a layer of
     * the picture rather than a separate window. */
    if (rmlui_active()) {
        SDL_SetRenderTarget(R, target);
        rmlui_render();
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
        fprintf(stderr, "render: %dx%d of game scaled x%.3f per quad into a %dx%d target, "
                        "filling %.0fx%.0f at (%.0f,%.0f), lighting %s\n",
                w, h, (double)scale, ow, oh, (double)place.w, (double)place.h,
                (double)ox, (double)oy,
                (engine_enabled() && opt_lighting()) ? "on" : "OFF");
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

/* The retained frame, the overlay boundary and the pool bookkeeping all live in FrameLife
 * (framelife.h) so they can be walked by `ctest framelife` without a window or a GPU. What
 * stays here is only what has an SDL object attached to it. */

void render_frame_reset(void) { fl_frame_reset(&FL); }

/* The port is about to draw its own UI over a LIVE frame -- the controls hint on a menu the
 * game is still updating. Same boundary as render_hold_begin sets for a frozen one. */
void render_overlay_mark(uint32_t dst_pixels)
{
    if (!render_gpu_enabled()) return;
    for (int i = 0; i < FL.nlists; i++)
        if (lists[i].dst_pixels == dst_pixels) fl_overlay_mark(&FL, i);
}

/* Draw over the frame that is already there, without clearing it. Returns 0 when there is no
 * retained frame -- the GPU path is off, or nothing has been drawn yet -- and the caller must
 * then do whatever it does without the renderer.
 *
 * Called once per held frame, and it REWINDS: the previous held frame's overlay is dropped
 * before this one's is recorded, or the pause menu would be appended to itself sixty times a
 * second until the list filled. The tile arena and the texture pool are rewound with it,
 * which is what stops the pool leaking a texture per frame while the game sits paused. */
int render_hold_begin(void)
{
    if (!render_gpu_enabled() || !R) return 0;
    /* The entries about to be rewound away are holding pooled textures; forget them here,
     * because fl_hold_begin releases the pool slots underneath them. */
    int was[FL_LISTS_MAX];
    for (int i = 0; i < FL.nlists; i++) was[i] = FL.n[i];
    if (!fl_hold_begin(&FL)) return 0;
    for (int i = 0; i < FL.nlists; i++)
        for (int j = FL.n[i]; j < was[i]; j++) lists[i].e[j].tile_tex = NULL;
    tile_list = -1;
    return 1;
}

/* Prints the zeros too, and says what a zero means. "0 quads" and "the renderer was never
 * called" are different faults with the same look. */
void render_report(void)
{
    if (!getenv("LF2_RENDER_DEBUG")) return;
    fprintf(stderr, "render: gpu=%s frames=%ld (software fallbacks=%ld) quads=%ld fills=%ld "
                    "tiles=%ld textures=%d uploads=%ld dropped=%ld mesh=%ld\n",
            render_gpu_enabled() ? "on" : "off", stat_frames, stat_soft_frames,
            stat_tex, stat_fill, stat_tile, ntexes, stat_uploads, stat_dropped, stat_mesh);
    /* The allocation count is the number that matters: it must go FLAT once the pool is warm.
     * A count that keeps climbing with the frame count means tiles of ever-changing sizes and
     * the GPU allocator is being churned again, which is what wedged a GPU once. */
    fprintf(stderr, "render: %ld tile draws served by %d pooled textures, %ld allocations "
                    "(%.3f per frame -- this must be near zero once warm); the busiest frame "
                    "drew %d tile(s) against a pool of %d\n",
            stat_tile, FL.pool_n, stat_tile_allocs,
            stat_frames ? (double)stat_tile_allocs / (double)stat_frames : 0.0,
            FL.peak_tiles, TILE_TEX_MAX);
    if (FL.pool_exhausted)
        fprintf(stderr, "render: %ld tiles were NOT DRAWN -- the %d-texture pool was full, so "
                        "text is MISSING from those frames\n",
                FL.pool_exhausted, TILE_TEX_MAX);
    /* Prints the zero and says what it means, because zero is the ordinary answer: a run
      * that never paused SHOULD report none, and a run that paused and reports none is the
      * pause menu having fallen back to the software compositor (issue #52). */
    fprintf(stderr, "render: %ld frame(s) were drawn over a RETAINED list -- the game recorded "
                    "nothing and the renderer redrew the frame it already had, which is how the "
                    "pause menu is presented. Zero is correct for a run that never paused.\n",
            FL.held_frames);
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
    mesh_report();
    engine_report();
    if (engine_enabled() && opt_lighting() && stat_ground && !stat_shadow)
        fprintf(stderr, "render: %ld ground markers but NO cast shadows -- every one was "
                        "followed by something that could not be drawn\n", stat_ground);
    if (engine_enabled() && opt_lighting() && stat_frames && !stat_ground)
        fprintf(stderr, "render: NO ground markers were seen, so no shadow was replaced -- "
                        "the stage's shadow object was never identified\n");
    if (render_gpu_enabled() && stat_frames == 0)
        fprintf(stderr, "render: the GPU path presented NO frames -- every one fell back to "
                        "the software compositor, so these counters describe nothing\n");
    if (stat_dropped)
        fprintf(stderr, "render: %ld entries were DROPPED (list or tile arena full), so the "
                        "frames above are incomplete\n", stat_dropped);
}
