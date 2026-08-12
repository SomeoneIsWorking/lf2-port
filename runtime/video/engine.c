/* The port's own rendering engine. See engine.h for why it exists and what it is not. */
#include "engine.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"

#include "../shaders/gen/quad_vert_spv.h"
#include "../shaders/gen/quad_spv.h"

/* ---- state ------------------------------------------------------------------------------ */

static SDL_Renderer  *R;
static SDL_GPUDevice *DEV;
static SDL_GPUSampler *SMP;

/* THREE PIPELINES, one per blend mode, because SDL_GPU fixes the blend state at pipeline
 * creation. The alternative -- premultiplying everything on upload so one blend serves all --
 * would change what a keyed sprite's colour IS, and the byte-identity arms of
 * tools/e2e.sh background compare exact pixels against the software blitter. Three pipelines
 * is a one-off cost at startup; a changed colour is a whole class of drift. */
enum { BLEND_NONE = 0, BLEND_ALPHA = 1, BLEND_PREMUL = 2, BLEND_KINDS = 3 };
static SDL_GPUGraphicsPipeline *PIPE[BLEND_KINDS];

/* The offscreen pair. Colour is wrapped as an ordinary SDL_Texture (claim C030 -- the same
 * object, no copy and no readback) so the existing present path can put it on the screen while
 * the engine is being brought up beside the old renderer rather than in place of it. */
static SDL_GPUTexture *tex_color, *tex_depth;
static SDL_Texture    *wrapped;
static int             tgt_w, tgt_h;

static SDL_GPUBuffer *vbuf;
static SDL_GPUTransferBuffer *vxfer;
static int vbuf_cap;

static int  init_done, init_ok, enabled = -1;
static const char *init_why = "not attempted";
static long stat_frames, stat_quads, stat_batches, stat_uploads, stat_dropped;

typedef struct { float x, y, depth, u, v, r, g, b, a; } QuadVertex;

/* ---- the texture cache -------------------------------------------------------------------
 *
 * ONE POOL, and that is defect 2 of issue #64 gone: the geometry pass and the sprite pass
 * sample the same object because there is one pass. The logic is render.c's, which was never
 * the part that was wrong -- a content hash so a surface the game reloads into the same arena
 * slot cannot keep drawing last level's art, and the colour key turned into ALPHA on upload,
 * which is where this port's blend stage comes from at all.
 */
enum { TEX_MAX = 512 };
typedef struct {
    uint32_t pixels;            /* guest address, or 0 for a host tile */
    const void *host;           /* host_rgba, for GDI text the port rasterises itself */
    int      keyed;
    uint32_t key_lo, key_hi;
    int      w, h;
    uint32_t content;
    SDL_GPUTexture *tex;
} EngTex;
static EngTex texes[TEX_MAX];
static int    ntexes;

/* Sampled every 7th row, exactly as render.c does: enough to catch a different picture, cheap
 * enough to run per draw. Same stride on purpose -- two hashes that disagreed about what
 * "changed" would make the two paths cache differently and the A/B meaningless. */
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

/* Guest ARGB (or host RGBA) into the RGBA8 the pipeline samples, with the key becoming alpha.
 *
 * The guest's surfaces are 32-bit XRGB with no meaningful alpha, so the byte order has to be
 * swizzled here rather than by choosing a format: SDL_GPU has no B8G8R8A8 guaranteed across
 * backends, and picking one that happens to exist on this machine is how a port works on the
 * machine it was written on. */
static void fill_rgba(uint8_t *dst, const EngineQuad *q, int w, int h)
{
    if (q->host_argb) {
        /* ARGB words to RGBA bytes, alpha KEPT -- these are premultiplied glyph tiles and the
         * alpha is their coverage. A memcpy here would have been the same bytes in the wrong
         * order: red and blue swapped, which on white text looks like nothing at all and on
         * anything coloured looks like a palette bug a long way from here. */
        for (int y = 0; y < h; y++) {
            const uint32_t *src = (const uint32_t *)((const uint8_t *)q->host_argb
                                                     + (size_t)y * (size_t)q->host_pitch);
            uint8_t *row = dst + (size_t)y * (size_t)w * 4;
            for (int x = 0; x < w; x++) {
                const uint32_t v = src[x];
                row[x * 4 + 0] = (uint8_t)((v >> 16) & 0xff);
                row[x * 4 + 1] = (uint8_t)((v >> 8) & 0xff);
                row[x * 4 + 2] = (uint8_t)(v & 0xff);
                row[x * 4 + 3] = (uint8_t)((v >> 24) & 0xff);
            }
        }
        return;
    }
    const uint8_t *base = g_mem + q->src_pixels;
    const uint32_t lo = q->key_lo & 0x00ffffffu, hi = q->key_hi & 0x00ffffffu;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)(base + (size_t)y * (size_t)q->spitch);
        uint8_t *row = dst + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            const uint32_t v = src[x] & 0x00ffffffu;
            const int clear = q->keyed && v >= lo && v <= hi;
            row[x * 4 + 0] = (uint8_t)(clear ? 0 : (v >> 16) & 0xff);   /* R */
            row[x * 4 + 1] = (uint8_t)(clear ? 0 : (v >> 8) & 0xff);    /* G */
            row[x * 4 + 2] = (uint8_t)(clear ? 0 : v & 0xff);           /* B */
            row[x * 4 + 3] = (uint8_t)(clear ? 0 : 255);
        }
    }
}

static int tex_upload(EngTex *t, const EngineQuad *q)
{
    const int w = t->w, h = t->h;
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(DEV, &ti);
    void *map = tb ? SDL_MapGPUTransferBuffer(DEV, tb, false) : NULL;
    if (!map) {
        fprintf(stderr, "engine: a %dx%d upload could not be mapped: %s\n", w, h, SDL_GetError());
        if (tb) SDL_ReleaseGPUTransferBuffer(DEV, tb);
        return 0;
    }
    fill_rgba((uint8_t *)map, q, w, h);
    SDL_UnmapGPUTransferBuffer(DEV, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = { tb, 0, (Uint32)w, (Uint32)h };
    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = t->tex; dst.w = (Uint32)w; dst.h = (Uint32)h; dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(DEV, tb);
    stat_uploads++;
    return 1;
}

static SDL_GPUTexture *tex_for(const EngineQuad *q)
{
    const int w = q->host_argb ? q->host_w : q->sw;
    const int h = q->host_argb ? q->host_h : q->sh;
    if (w <= 0 || h <= 0) return NULL;

    /* A host tile is keyed on its POINTER and its size. The port owns that memory and rewrites
     * it per frame, so it is hashed too -- the pool is what stops a texture being allocated per
     * glyph per frame, which is the churn that wedged a GPU once (issue #40). */
    const uint32_t content = q->host_argb
        ? sample_hash((const uint8_t *)q->host_argb, w, h, q->host_pitch)
        : sample_hash(g_mem + q->src_pixels, w, h, q->spitch);

    for (int i = 0; i < ntexes; i++) {
        EngTex *t = &texes[i];
        if (t->w != w || t->h != h || t->keyed != q->keyed) continue;
        if (q->host_argb ? (t->host != q->host_argb) : (t->pixels != q->src_pixels)) continue;
        if (q->keyed && (t->key_lo != q->key_lo || t->key_hi != q->key_hi)) continue;
        if (t->content != content) { t->content = content; tex_upload(t, q); }
        return t->tex;
    }
    if (ntexes >= TEX_MAX) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "engine: the %d-texture pool is FULL; further sheets are NOT drawn "
                            "-- art is missing from these frames, it is not a slow path\n",
                    TEX_MAX);
        }
        return NULL;
    }
    EngTex *t = &texes[ntexes];
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.width = (Uint32)w; ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1; ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    t->tex = SDL_CreateGPUTexture(DEV, &ci);
    if (!t->tex) {
        fprintf(stderr, "engine: could not create a %dx%d texture: %s\n", w, h, SDL_GetError());
        return NULL;
    }
    t->pixels = q->host_argb ? 0u : q->src_pixels;
    t->host = q->host_argb;
    t->keyed = q->keyed; t->key_lo = q->key_lo; t->key_hi = q->key_hi;
    t->w = w; t->h = h; t->content = content;
    ntexes++;
    if (!tex_upload(t, q)) return NULL;
    return t->tex;
}

void engine_surface_dirty(uint32_t pixels)
{
    for (int i = 0; i < ntexes; i++)
        if (texes[i].pixels == pixels) texes[i].content = 0;   /* forces a re-hash and reupload */
}

/* ---- setup ------------------------------------------------------------------------------- */

static SDL_GPUShader *shader_make(const unsigned char *spv, size_t len,
                                  SDL_GPUShaderStage stage, int samplers, int uniforms,
                                  const char *what)
{
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code = spv;
    info.code_size = len;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = (Uint32)samplers;
    info.num_uniform_buffers = (Uint32)uniforms;
    SDL_GPUShader *s = SDL_CreateGPUShader(DEV, &info);
    if (!s) fprintf(stderr, "engine: the %s shader failed: %s\n", what, SDL_GetError());
    return s;
}

static void blend_state(SDL_GPUColorTargetBlendState *b, int kind)
{
    SDL_zero(*b);
    if (kind == BLEND_NONE) return;
    b->enable_blend = true;
    b->src_color_blendfactor = (kind == BLEND_PREMUL)
        ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    b->dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b->color_blend_op = SDL_GPU_BLENDOP_ADD;
    b->src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b->dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b->alpha_blend_op = SDL_GPU_BLENDOP_ADD;
}

int engine_init(SDL_Renderer *r)
{
    if (init_done) return init_ok;
    init_done = 1;
    R = r;

    DEV = SDL_GetGPURendererDevice(r);
    if (!DEV) {
        init_why = "the renderer has no GPU device";
        fprintf(stderr, "engine: the '%s' renderer has no GPU device, so the engine cannot run "
                        "and the SDL_Render path draws instead.\n", SDL_GetRendererName(r));
        return 0;
    }
    if (!(SDL_GetGPUShaderFormats(DEV) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        init_why = "the GPU backend does not take SPIR-V";
        fprintf(stderr, "engine: the %s backend does not accept SPIR-V, the only format this "
                        "port ships.\n", SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }
    if (!SDL_GPUTextureSupportsFormat(DEV, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                      SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        /* Not fatal the way it is for the geometry pass -- sprites arrive painter-ordered and
         * would still draw correctly without a depth buffer. It IS fatal to the reason this
         * engine exists, so it refuses rather than becoming a slower copy of what it replaces. */
        init_why = "no D32_FLOAT depth target";
        fprintf(stderr, "engine: the %s backend has no D32_FLOAT depth-stencil target. Sprites "
                        "would still draw, but a depth buffer shared with the stage geometry is "
                        "the whole reason for this engine, so it does NOT run here.\n",
                SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }

    SDL_GPUShader *vs = shader_make(quad_vert_spv, sizeof quad_vert_spv,
                                    SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, "quad vertex");
    SDL_GPUShader *fs = shader_make(quad_spv, sizeof quad_spv,
                                    SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, "quad fragment");
    if (!vs || !fs) {
        init_why = "a shader failed to compile";
        if (vs) SDL_ReleaseGPUShader(DEV, vs);
        if (fs) SDL_ReleaseGPUShader(DEV, fs);
        return 0;
    }

    SDL_GPUVertexBufferDescription vbd;
    SDL_zero(vbd);
    vbd.slot = 0;
    vbd.pitch = sizeof(QuadVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[4];
    SDL_zero(attrs);
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = (Uint32)offsetof(QuadVertex, x);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[1].offset = (Uint32)offsetof(QuadVertex, depth);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[2].offset = (Uint32)offsetof(QuadVertex, u);
    attrs[3].location = 3; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[3].offset = (Uint32)offsetof(QuadVertex, r);

    int made = 0;
    for (int k = 0; k < BLEND_KINDS; k++) {
        SDL_GPUColorTargetDescription ctd;
        SDL_zero(ctd);
        ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        blend_state(&ctd.blend_state, k);

        SDL_GPUGraphicsPipelineCreateInfo pi;
        SDL_zero(pi);
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
        pi.vertex_input_state.num_vertex_attributes = 4;
        pi.vertex_input_state.vertex_attributes = attrs;
        pi.target_info.num_color_targets = 1;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.has_depth_stencil_target = true;
        pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        /* THE DEPTH IS WRITTEN AND TESTED, but the picture is still the painter order's --
         * see engine_draw for why those are not in conflict. LESS_OR_EQUAL rather than LESS:
         * two quads at the same list position (a batch) share a depth, and LESS would drop
         * every one after the first. */
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        PIPE[k] = SDL_CreateGPUGraphicsPipeline(DEV, &pi);
        if (PIPE[k]) made++;
        else fprintf(stderr, "engine: blend pipeline %d failed: %s\n", k, SDL_GetError());
    }
    SDL_ReleaseGPUShader(DEV, vs);
    SDL_ReleaseGPUShader(DEV, fs);
    if (made != BLEND_KINDS) {
        init_why = "a blend pipeline failed";
        return 0;
    }

    SDL_GPUSamplerCreateInfo si;
    SDL_zero(si);
    /* NEAREST, always: this is pixel art magnified two or three times, and the frame is built
     * at the window's resolution so a sprite is resampled exactly once. */
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SMP = SDL_CreateGPUSampler(DEV, &si);
    if (!SMP) { init_why = "no sampler"; return 0; }

    init_ok = 1;
    init_why = "ready";
    fprintf(stderr, "engine: up on the %s backend, sharing the renderer's device "
                    "(D32_FLOAT depth, one texture pool)\n", SDL_GetGPUDeviceDriver(DEV));
    return 1;
}

int engine_ready(void) { return init_ok; }

/* LF2_ENGINE selects which renderer draws. It is a DIAGNOSTIC and an A/B control arm, not a
 * feature switch: the engine becomes the default when it is shown to match the software
 * compositor on the frames tools/e2e.sh render already compares, and the old path stays
 * selectable so the two can go on being diffed -- exactly as LF2_BG_ORIG does for the
 * background override. A reimplementation that cannot be diffed against what it replaces is a
 * rewrite. */
int engine_enabled(void)
{
    if (enabled < 0) {
        const char *v = getenv("LF2_ENGINE");
        enabled = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
    }
    return enabled && init_ok;
}

/* ---- targets and buffers ------------------------------------------------------------------ */

static void targets_release(void)
{
    if (wrapped)   { SDL_DestroyTexture(wrapped); wrapped = NULL; }
    if (tex_color) { SDL_ReleaseGPUTexture(DEV, tex_color); tex_color = NULL; }
    if (tex_depth) { SDL_ReleaseGPUTexture(DEV, tex_depth); tex_depth = NULL; }
    tgt_w = tgt_h = 0;
}

static int targets_make(int w, int h)
{
    if (tex_color && tgt_w == w && tgt_h == h) return 1;
    targets_release();

    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = (Uint32)w; ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1; ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_color = SDL_CreateGPUTexture(DEV, &ci);

    ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tex_depth = SDL_CreateGPUTexture(DEV, &ci);

    if (!tex_color || !tex_depth) {
        fprintf(stderr, "engine: could not allocate the %dx%d target pair: %s\n",
                w, h, SDL_GetError());
        targets_release();
        return 0;
    }

    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, tex_color);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    wrapped = SDL_CreateTextureWithProperties(R, p);
    SDL_DestroyProperties(p);
    if (!wrapped) {
        fprintf(stderr, "engine: the colour target could not be wrapped: %s\n", SDL_GetError());
        targets_release();
        return 0;
    }
    SDL_SetTextureScaleMode(wrapped, SDL_SCALEMODE_NEAREST);
    tgt_w = w; tgt_h = h;
    return 1;
}

static int vbuf_reserve(int bytes)
{
    if (vbuf && vbuf_cap >= bytes) return 1;
    if (vbuf)  { SDL_ReleaseGPUBuffer(DEV, vbuf); vbuf = NULL; }
    if (vxfer) { SDL_ReleaseGPUTransferBuffer(DEV, vxfer); vxfer = NULL; }

    SDL_GPUBufferCreateInfo bi;
    SDL_zero(bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = (Uint32)bytes;
    vbuf = SDL_CreateGPUBuffer(DEV, &bi);

    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)bytes;
    vxfer = SDL_CreateGPUTransferBuffer(DEV, &ti);

    if (!vbuf || !vxfer) {
        fprintf(stderr, "engine: could not allocate a %d-byte vertex buffer: %s\n",
                bytes, SDL_GetError());
        if (vbuf)  { SDL_ReleaseGPUBuffer(DEV, vbuf); vbuf = NULL; }
        if (vxfer) { SDL_ReleaseGPUTransferBuffer(DEV, vxfer); vxfer = NULL; }
        vbuf_cap = 0;
        return 0;
    }
    vbuf_cap = bytes;
    return 1;
}

/* ---- the draw ----------------------------------------------------------------------------- */

static void emit(QuadVertex *v, const EngineQuad *q, float depth)
{
    const float x0 = q->x, y0 = q->y, x1 = q->x + q->w, y1 = q->y + q->h;
    const QuadVertex a = { x0, y0, depth, q->u0, q->v0, q->r, q->g, q->b, q->a };
    const QuadVertex b = { x1, y0, depth, q->u1, q->v0, q->r, q->g, q->b, q->a };
    const QuadVertex c = { x1, y1, depth, q->u1, q->v1, q->r, q->g, q->b, q->a };
    const QuadVertex d = { x0, y1, depth, q->u0, q->v1, q->r, q->g, q->b, q->a };
    v[0] = a; v[1] = b; v[2] = c;
    v[3] = a; v[4] = c; v[5] = d;
}

SDL_Texture *engine_draw(const EngineQuad *q, int n, int w, int h)
{
    if (!init_ok || n <= 0 || w <= 0 || h <= 0) return NULL;
    if (!targets_make(w, h)) return NULL;
    const int verts = n * 6;
    if (!vbuf_reserve(verts * (int)sizeof(QuadVertex))) return NULL;

    /* THE DEPTH IS THE LIST POSITION, and this is the one design decision in the file.
     *
     * The display list is painter-ordered, and that order is the GAME's answer -- it sorts its
     * own sprites on z, and the port learned the object/shadow pairing from the order the game
     * draws them in (C019). The engine must not second-guess it, or the first frame it drew
     * would differ from the software compositor for a reason that has nothing to do with the
     * engine being right.
     *
     * So the ordinal becomes the depth: later in the list is NEARER, i.e. a smaller z, and the
     * test is LESS_OR_EQUAL. Every quad therefore passes, and the picture is exactly the
     * painter order's -- while the depth buffer ends up holding a real, usable value for every
     * pixel. That is what the old arrangement could not have at any price, and it is what the
     * lighting step needs next.
     *
     * The +1s keep both ends off the clip planes: at n=1 the single quad lands at 0.5 rather
     * than at 0 or 1, where a driver may clip it. */
    void *map = SDL_MapGPUTransferBuffer(DEV, vxfer, false);
    if (!map) {
        fprintf(stderr, "engine: the vertex buffer could not be mapped: %s\n", SDL_GetError());
        return NULL;
    }
    QuadVertex *vp = (QuadVertex *)map;
    for (int i = 0; i < n; i++)
        emit(vp + (size_t)i * 6, &q[i], 1.0f - (float)(i + 1) / (float)(n + 1));
    SDL_UnmapGPUTransferBuffer(DEV, vxfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    if (!cmd) { fprintf(stderr, "engine: no command buffer: %s\n", SDL_GetError()); return NULL; }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation from = { vxfer, 0 };
    SDL_GPUBufferRegion into = { vbuf, 0, (Uint32)(verts * (int)sizeof(QuadVertex)) };
    SDL_UploadToGPUBuffer(copy, &from, &into, false);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = tex_color;
    /* Opaque black, not transparent: this is the whole frame, not an overlay over one. A
     * transparent clear would let whatever the present path had behind it show through the
     * columns no quad covers, which is issue #29's ghost by another route. */
    cti.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = tex_depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_STORE;      /* STORED -- the lighting step reads it */
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &dti);
    if (!pass) {
        fprintf(stderr, "engine: the render pass could not begin: %s\n", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return NULL;
    }
    SDL_GPUBufferBinding vb = { vbuf, 0 };
    const float view[4] = { (float)w, (float)h, 0.0f, 0.0f };

    /* Consecutive quads sharing a texture and a blend mode go in one draw. Consecutive is the
     * only grouping allowed: reordering to gather more would change the painter order, and the
     * depth above is that order. A frame of a fighter's animation is dozens of quads off one
     * sheet, so consecutive already collapses most of them. */
    int i = 0, bound_pipe = -1;
    SDL_GPUTexture *bound_tex = NULL;
    while (i < n) {
        SDL_GPUTexture *t = (q[i].src_pixels || q[i].host_argb) ? tex_for(&q[i]) : NULL;
        const int kind = q[i].blend < 0 || q[i].blend >= BLEND_KINDS ? BLEND_ALPHA : q[i].blend;
        int j = i + 1;
        while (j < n && j - i < 4096) {
            const int k2 = q[j].blend < 0 || q[j].blend >= BLEND_KINDS ? BLEND_ALPHA : q[j].blend;
            if (k2 != kind) break;
            SDL_GPUTexture *t2 = (q[j].src_pixels || q[j].host_argb) ? tex_for(&q[j]) : NULL;
            if (t2 != t) break;
            j++;
        }
        if (!t && (q[i].src_pixels || q[i].host_argb)) { stat_dropped += j - i; i = j; continue; }

        if (bound_pipe != kind) { SDL_BindGPUGraphicsPipeline(pass, PIPE[kind]); bound_pipe = kind; }
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        /* A sampler must be bound whether or not there is art: the fragment shader declares
         * one, and leaving it unbound is undefined rather than "the branch is not taken". The
         * colour target stands in when there is nothing to sample -- u_flags.x is 0, so nothing
         * reads it. */
        SDL_GPUTextureSamplerBinding tsb = { t ? t : tex_color, SMP };
        if (bound_tex != tsb.texture) { bound_tex = tsb.texture; }
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        const float flags[4] = { t ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        SDL_PushGPUVertexUniformData(cmd, 0, view, sizeof view);
        SDL_PushGPUFragmentUniformData(cmd, 0, flags, sizeof flags);
        SDL_DrawGPUPrimitives(pass, (Uint32)((j - i) * 6), 1, (Uint32)(i * 6), 0);
        stat_batches++;
        i = j;
    }
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    stat_frames++;
    stat_quads += n;
    return wrapped;
}

void engine_report(void)
{
    if (!getenv("LF2_ENGINE_DEBUG")) return;
    fprintf(stderr, "engine: %s (%s). %ld frame(s), %ld quad(s) in %ld batch(es), %d texture(s), "
                    "%ld upload(s), %ld quad(s) DROPPED\n",
            engine_enabled() ? "DRAWING" : "not drawing", init_why,
            stat_frames, stat_quads, stat_batches, ntexes, stat_uploads, stat_dropped);
    /* The zero is printed and named, because zero is the ordinary answer when the engine is
     * built but not selected -- and "built but not selected" must not look like "selected and
     * drew nothing". */
    if (engine_enabled() && !stat_frames)
        fprintf(stderr, "engine: it is SELECTED and has drawn NOTHING -- no frame reached it, "
                        "which is a different fault from a frame that came out wrong\n");
    if (stat_dropped)
        fprintf(stderr, "engine: %ld quad(s) were dropped for want of a texture; art is MISSING "
                        "from those frames\n", stat_dropped);
}

void engine_shutdown(void)
{
    if (!DEV) return;
    for (int i = 0; i < ntexes; i++)
        if (texes[i].tex) SDL_ReleaseGPUTexture(DEV, texes[i].tex);
    ntexes = 0;
    targets_release();
    if (vbuf)  { SDL_ReleaseGPUBuffer(DEV, vbuf); vbuf = NULL; }
    if (vxfer) { SDL_ReleaseGPUTransferBuffer(DEV, vxfer); vxfer = NULL; }
    if (SMP)   { SDL_ReleaseGPUSampler(DEV, SMP); SMP = NULL; }
    for (int k = 0; k < BLEND_KINDS; k++)
        if (PIPE[k]) { SDL_ReleaseGPUGraphicsPipeline(DEV, PIPE[k]); PIPE[k] = NULL; }
    vbuf_cap = 0;
}
