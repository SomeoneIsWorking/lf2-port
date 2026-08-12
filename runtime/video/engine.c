/* The port's own rendering engine. See engine.h for why it exists and what it is not. */
#include "engine.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"

#include "mesh.h"
#include "hd2d.h"

#include "../shaders/gen/quad_vert_spv.h"
#include "../shaders/gen/quad_spv.h"
#include "../shaders/gen/mesh_vert_spv.h"
#include "../shaders/gen/mesh_spv.h"
#include "../shaders/gen/dof_spv.h"

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

/* The GEOMETRY pipeline, in the same pass and against the same depth buffer as the quads above.
 * Alpha-blended, because a set is drawn over the game's painted layers and every texel its
 * geometry does not cover has to let them through. */
static SDL_GPUGraphicsPipeline *GPIPE;
static SDL_GPUBuffer *gvbuf;
static SDL_GPUTransferBuffer *gvxfer;
static int gvbuf_cap;
static long stat_geom_draws, stat_geom_tris;

/* THE DEFOCUS (issue #63). A render state rather than a pipeline, because it is a pass over the
 * finished frame through SDL_Render -- the same shape hd2d's lighting uses. Rebuilt whenever the
 * G-buffer is, since it holds a binding to it. */
static SDL_GPUShader     *sh_dof;
static SDL_GPURenderState *st_dof;
static long stat_dof_frames;
static int  dof_on = -1;

static void gbuf_report(int w, int h);

/* The offscreen pair. Colour is wrapped as an ordinary SDL_Texture (claim C030 -- the same
 * object, no copy and no readback) so the existing present path can put it on the screen while
 * the engine is being brought up beside the old renderer rather than in place of it. */
static SDL_GPUTexture *tex_color, *tex_depth;
static SDL_Texture    *wrapped;
/* THE G-BUFFER (issue #63): a second colour attachment carrying a surface normal and the draw's
 * real DISTANCE. It is a colour target rather than the depth buffer because SDL3 has no pixel
 * format for depth at all -- checked, `SDL_pixels.h` declares none -- so a D32_FLOAT texture
 * cannot be wrapped as an SDL_Texture and nothing outside the engine could ever sample it.
 *
 * R16G16B16A16_FLOAT rather than 8-bit: the alpha channel is a distance running from about 0.89
 * (a foreground strip) to 535 (a distant sky) on the shipped stages, and eight bits of that is a
 * staircase, which a defocus would turn into visible banding by distance. */
static SDL_GPUTexture *tex_gbuf;
static SDL_Texture    *wrapped_gbuf;
static int             tgt_w, tgt_h;

static SDL_GPUBuffer *vbuf;
static SDL_GPUTransferBuffer *vxfer;
static int vbuf_cap;

static int  init_done, init_ok, enabled = -1;
static const char *init_why = "not attempted";
static long stat_frames, stat_quads, stat_batches, stat_uploads, stat_dropped;

typedef struct { float x, y, depth, u, v, r, g, b, a, world; } QuadVertex;

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

    SDL_GPUVertexAttribute attrs[5];
    SDL_zero(attrs);
    attrs[4].location = 4; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[4].offset = (Uint32)offsetof(QuadVertex, world);
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
        SDL_GPUColorTargetDescription ctd[2];
        SDL_zero(ctd);
        ctd[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        blend_state(&ctd[0].blend_state, k);
        /* The G-buffer is NEVER blended, whatever the colour target does. A distance is not a
         * quantity you can average with the one behind it: half way between a sprite at 1.0 and
         * a sky at 535 is a plane that nothing in the scene occupies, and a defocus driven off
         * it would blur a hard edge into a smooth ramp of wrong distances. The nearest writer
         * wins outright, which is what the depth test already decides. */
        ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

        SDL_GPUGraphicsPipelineCreateInfo pi;
        SDL_zero(pi);
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.vertex_input_state.vertex_attributes = attrs;
        pi.target_info.num_color_targets = 2;
        pi.target_info.color_target_descriptions = ctd;
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

    /* ---- the geometry pipeline, sharing everything ---- */
    {
        SDL_GPUShader *gvs = shader_make(mesh_vert_spv, sizeof mesh_vert_spv,
                                         SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, "geometry vertex");
        SDL_GPUShader *gfs = shader_make(mesh_spv, sizeof mesh_spv,
                                         SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, "geometry fragment");
        if (gvs && gfs) {
            SDL_GPUVertexBufferDescription gvbd;
            SDL_zero(gvbd);
            gvbd.slot = 0;
            gvbd.pitch = sizeof(MeshVertex);
            gvbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            SDL_GPUVertexAttribute ga[4];
            SDL_zero(ga);
            ga[0].location = 0; ga[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            ga[0].offset = (Uint32)offsetof(MeshVertex, x);
            ga[1].location = 1; ga[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            ga[1].offset = (Uint32)offsetof(MeshVertex, u);
            ga[2].location = 2; ga[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            ga[2].offset = (Uint32)offsetof(MeshVertex, nx);
            ga[3].location = 3; ga[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            ga[3].offset = (Uint32)offsetof(MeshVertex, r);

            SDL_GPUColorTargetDescription gct[2];
            SDL_zero(gct);
            gct[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            blend_state(&gct[0].blend_state, BLEND_ALPHA);
            gct[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

            SDL_GPUGraphicsPipelineCreateInfo gp;
            SDL_zero(gp);
            gp.vertex_shader = gvs;
            gp.fragment_shader = gfs;
            gp.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            gp.vertex_input_state.num_vertex_buffers = 1;
            gp.vertex_input_state.vertex_buffer_descriptions = &gvbd;
            gp.vertex_input_state.num_vertex_attributes = 4;
            gp.vertex_input_state.vertex_attributes = ga;
            gp.target_info.num_color_targets = 2;
            gp.target_info.color_target_descriptions = gct;
            gp.target_info.has_depth_stencil_target = true;
            gp.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            /* LESS here, not LESS_OR_EQUAL: within a set the depth is REAL and a genuine tie is
             * coplanar geometry, where either answer is as good. The quads use OR_EQUAL only
             * because a batch shares one ordinal by construction. */
            gp.depth_stencil_state.enable_depth_test = true;
            gp.depth_stencil_state.enable_depth_write = true;
            gp.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
            gp.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            gp.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            GPIPE = SDL_CreateGPUGraphicsPipeline(DEV, &gp);
            if (!GPIPE)
                fprintf(stderr, "engine: the geometry pipeline failed: %s -- sprites draw, "
                                "hand-woven sets do NOT\n", SDL_GetError());
        }
        if (gvs) SDL_ReleaseGPUShader(DEV, gvs);
        if (gfs) SDL_ReleaseGPUShader(DEV, gfs);
    }

    sh_dof = shader_make(dof_spv, sizeof dof_spv, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1, "defocus");
    if (!sh_dof)
        fprintf(stderr, "engine: no defocus shader -- frames are presented unblurred, which is "
                        "a picture without a depth of field rather than a broken one\n");

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
    if (wrapped)      { SDL_DestroyTexture(wrapped); wrapped = NULL; }
    if (wrapped_gbuf) { SDL_DestroyTexture(wrapped_gbuf); wrapped_gbuf = NULL; }
    if (tex_color) { SDL_ReleaseGPUTexture(DEV, tex_color); tex_color = NULL; }
    if (tex_depth) { SDL_ReleaseGPUTexture(DEV, tex_depth); tex_depth = NULL; }
    if (tex_gbuf)  { SDL_ReleaseGPUTexture(DEV, tex_gbuf); tex_gbuf = NULL; }
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

    ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_gbuf = SDL_CreateGPUTexture(DEV, &ci);

    ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tex_depth = SDL_CreateGPUTexture(DEV, &ci);

    if (!tex_color || !tex_depth || !tex_gbuf) {
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
    if (st_dof)   { SDL_DestroyGPURenderState(st_dof); st_dof = NULL; }

    SDL_PropertiesID pg = SDL_CreateProperties();
    SDL_SetPointerProperty(pg, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, tex_gbuf);
    SDL_SetNumberProperty(pg, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(pg, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(pg, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                          SDL_PIXELFORMAT_ABGR64_FLOAT);
    wrapped_gbuf = SDL_CreateTextureWithProperties(R, pg);
    SDL_DestroyProperties(pg);
    if (!wrapped_gbuf) {
        /* NOT fatal: the picture is unaffected and only distance-driven effects lose their
         * input. Said out loud, because a silently absent G-buffer would make a depth of field
         * do nothing and look like a switch that was never wired up. */
        fprintf(stderr, "engine: the G-buffer could not be wrapped as a texture (%s) -- the "
                        "frame is unaffected, but nothing can read distance from it\n",
                SDL_GetError());
    } else {
        /* LINEAR on the G-buffer would average two distances at a texel boundary and invent a
         * plane nothing in the scene occupies -- the same reason its attachment is never
         * blended. NEAREST, so every sample is a distance something actually is at. */
        SDL_SetTextureScaleMode(wrapped_gbuf, SDL_SCALEMODE_NEAREST);
        if (sh_dof) {
            SDL_GPUTextureSamplerBinding b = { tex_gbuf, SMP };
            SDL_GPURenderStateCreateInfo ci;
            SDL_zero(ci);
            ci.fragment_shader = sh_dof;
            ci.num_sampler_bindings = 1;
            ci.sampler_bindings = &b;
            st_dof = SDL_CreateGPURenderState(R, &ci);
            if (!st_dof)
                fprintf(stderr, "engine: could not create the defocus render state (%s) -- the "
                                "frame is presented unblurred\n", SDL_GetError());
        }
    }
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
    const float wd = q->world_depth;
    const QuadVertex a = { x0, y0, depth, q->u0, q->v0, q->r, q->g, q->b, q->a, wd };
    const QuadVertex b = { x1, y0, depth, q->u1, q->v0, q->r, q->g, q->b, q->a, wd };
    const QuadVertex c = { x1, y1, depth, q->u1, q->v1, q->r, q->g, q->b, q->a, wd };
    const QuadVertex d = { x0, y1, depth, q->u0, q->v1, q->r, q->g, q->b, q->a, wd };
    v[0] = a; v[1] = b; v[2] = c;
    v[3] = a; v[4] = c; v[5] = d;
}

/* One gap's geometry, uploaded into its own buffer. Separate from the quad buffer because the
 * vertex FORMAT differs -- a MeshVertex carries four position channels and a normal. */
static int gvbuf_reserve(int bytes)
{
    if (gvbuf && gvbuf_cap >= bytes) return 1;
    if (gvbuf)  { SDL_ReleaseGPUBuffer(DEV, gvbuf); gvbuf = NULL; }
    if (gvxfer) { SDL_ReleaseGPUTransferBuffer(DEV, gvxfer); gvxfer = NULL; }
    SDL_GPUBufferCreateInfo bi;
    SDL_zero(bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = (Uint32)bytes;
    gvbuf = SDL_CreateGPUBuffer(DEV, &bi);
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)bytes;
    gvxfer = SDL_CreateGPUTransferBuffer(DEV, &ti);
    if (!gvbuf || !gvxfer) {
        fprintf(stderr, "engine: could not allocate a %d-byte geometry buffer: %s\n",
                bytes, SDL_GetError());
        if (gvbuf)  { SDL_ReleaseGPUBuffer(DEV, gvbuf); gvbuf = NULL; }
        if (gvxfer) { SDL_ReleaseGPUTransferBuffer(DEV, gvxfer); gvxfer = NULL; }
        gvbuf_cap = 0;
        return 0;
    }
    gvbuf_cap = bytes;
    return 1;
}

/* The light, read from hd2d rather than copied. stagelight.h is the one source and mesh.c reads
 * it the same way -- a second copy here is the exact bug issue #62's note records. */
typedef struct { float dir[4], sky[4], ground[4], tint[4]; } GeomLight;
static GeomLight geom_light(void)
{
    GeomLight u = {
        { 0.0f, 1.0f, 0.0f, 0.85f },
        { 0.34f, 0.36f, 0.42f, 0.0f },
        { 0.20f, 0.18f, 0.16f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
    };
    float d[3];
    hd2d_light_vector(d);
    if (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f) {
        u.dir[0] = d[0]; u.dir[1] = d[1]; u.dir[2] = d[2];
    }
    return u;
}

SDL_Texture *engine_draw(const EngineQuad *q, int n, const EngineGeom *g, int ng, int w, int h)
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

    /* The geometry, concatenated into one buffer with each piece's offset remembered, so the
     * whole frame is a single upload however many gaps a set occupies. */
    int gtotal = 0;
    for (int k = 0; k < ng; k++) if (GPIPE && g[k].v && g[k].n > 0) gtotal += g[k].n;
    int goff[64];
    int gused = 0;
    if (gtotal > 0 && gvbuf_reserve(gtotal * (int)sizeof(MeshVertex))) {
        void *gmap = SDL_MapGPUTransferBuffer(DEV, gvxfer, false);
        if (gmap) {
            MeshVertex *gp = (MeshVertex *)gmap;
            int at = 0;
            for (int k = 0; k < ng && gused < (int)(sizeof goff / sizeof goff[0]); k++) {
                if (!GPIPE || !g[k].v || g[k].n <= 0) { goff[k] = -1; continue; }
                goff[k] = at;
                memcpy(gp + at, g[k].v, (size_t)g[k].n * sizeof(MeshVertex));
                at += g[k].n;
                gused++;
            }
            SDL_UnmapGPUTransferBuffer(DEV, gvxfer);
        } else {
            gtotal = 0;
        }
    } else {
        gtotal = 0;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation from = { vxfer, 0 };
    SDL_GPUBufferRegion into = { vbuf, 0, (Uint32)(verts * (int)sizeof(QuadVertex)) };
    SDL_UploadToGPUBuffer(copy, &from, &into, false);
    if (gtotal > 0) {
        SDL_GPUTransferBufferLocation gfrom = { gvxfer, 0 };
        SDL_GPUBufferRegion ginto = { gvbuf, 0, (Uint32)(gtotal * (int)sizeof(MeshVertex)) };
        SDL_UploadToGPUBuffer(copy, &gfrom, &ginto, false);
    }
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo cti[2];
    SDL_zero(cti);
    cti[0].texture = tex_color;
    /* Opaque black, not transparent: this is the whole frame, not an overlay over one. A
     * transparent clear would let whatever the present path had behind it show through the
     * columns no quad covers, which is issue #29's ghost by another route. */
    cti[0].clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };
    cti[0].load_op = SDL_GPU_LOADOP_CLEAR;
    cti[0].store_op = SDL_GPU_STOREOP_STORE;

    /* The G-buffer clears to a ZERO normal and a ZERO distance, which both read as "nothing
     * known here" -- the same answer a sprite gives. Clearing the distance to something plausible
     * instead, like the fighters' plane, would make every uncovered pixel claim to be at a
     * distance nothing in the scene is at. */
    cti[1].texture = tex_gbuf;
    cti[1].clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 0.0f };
    cti[1].load_op = SDL_GPU_LOADOP_CLEAR;
    cti[1].store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = tex_depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_STORE;      /* STORED -- the lighting step reads it */
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, cti, 2, &dti);
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
    int gnext = 0;
    while (i < n) {
        /* THE GEOMETRY GOES IN HERE, in the middle of the quad stream, into the same depth
         * buffer -- which is the whole of issue #64's defect 3 gone. It used to be a separate
         * render pass into its own colour+depth pair per gap, composited back as a texture,
         * because the two renderers could only meet as a texture.
         *
         * Its depth sliver is the gap between this quad's ordinal and the previous one's, so it
         * is ordered against the game's layers by where the port placed it in the list and
         * against other geometry in the same sliver by its own parallax depth. */
        while (gnext < ng && g[gnext].at <= i) {
            const int k = gnext++;
            if (gtotal <= 0 || !GPIPE || goff[k] < 0 || g[k].n <= 0) continue;
            const float hi = 1.0f - (float)i / (float)(n + 1);
            const float lo = 1.0f - (float)(i + 1) / (float)(n + 1);
            SDL_BindGPUGraphicsPipeline(pass, GPIPE);
            SDL_GPUBufferBinding gvb = { gvbuf, 0 };
            SDL_BindGPUVertexBuffers(pass, 0, &gvb, 1);
            SDL_GPUTextureSamplerBinding gts = { tex_color, SMP };
            SDL_BindGPUFragmentSamplers(pass, 0, &gts, 1);
            const float cam[12] = {
                (float)g[k].camera, 0.0f, 0.0f, 0.0f,
                g[k].sx_scale, g[k].sx_bias, g[k].sy_scale, g[k].sy_bias,
                lo, hi, 0.0f, 0.0f,
            };
            const GeomLight gl = geom_light();
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof cam);
            SDL_PushGPUFragmentUniformData(cmd, 0, &gl, sizeof gl);
            SDL_DrawGPUPrimitives(pass, (Uint32)g[k].n, 1, (Uint32)goff[k], 0);
            stat_geom_draws++;
            stat_geom_tris += g[k].n / 3;
            bound_pipe = -1;            /* the quad pipeline must be rebound after this */
        }

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
    /* Anything the port placed after the last quad -- geometry nearer than every layer, still
     * behind the sprites the game goes on placing itself. */
    while (gnext < ng) {
        const int k = gnext++;
        if (gtotal <= 0 || !GPIPE || goff[k] < 0 || g[k].n <= 0) continue;
        const float hi = 1.0f - (float)(n - 1) / (float)(n + 1);
        const float lo = 1.0f - (float)n / (float)(n + 1);
        SDL_BindGPUGraphicsPipeline(pass, GPIPE);
        SDL_GPUBufferBinding gvb = { gvbuf, 0 };
        SDL_BindGPUVertexBuffers(pass, 0, &gvb, 1);
        SDL_GPUTextureSamplerBinding gts = { tex_color, SMP };
        SDL_BindGPUFragmentSamplers(pass, 0, &gts, 1);
        const float cam[12] = {
            (float)g[k].camera, 0.0f, 0.0f, 0.0f,
            g[k].sx_scale, g[k].sx_bias, g[k].sy_scale, g[k].sy_bias,
            lo, hi, 0.0f, 0.0f,
        };
        const GeomLight gl = geom_light();
        SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof cam);
        SDL_PushGPUFragmentUniformData(cmd, 0, &gl, sizeof gl);
        SDL_DrawGPUPrimitives(pass, (Uint32)g[k].n, 1, (Uint32)goff[k], 0);
        stat_geom_draws++;
        stat_geom_tris += g[k].n / 3;
    }

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    stat_frames++;
    stat_quads += n;
    gbuf_report(w, h);
    return wrapped;
}

/* IEEE half to float. Written out rather than reached for, because the G-buffer readback is the
 * only thing in the port that reads a 16-bit float and a wrong decode here would show up as
 * plausible-but-wrong distances -- which is exactly the kind of number nobody double-checks. */
static float half_to_float(uint16_t h)
{
    const uint32_t sgn = (uint32_t)(h >> 15) & 1u;
    const uint32_t exp = (uint32_t)(h >> 10) & 0x1fu;
    const uint32_t man = (uint32_t)h & 0x3ffu;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) bits = sgn << 31;
        else {
            int e = -1;
            uint32_t m = man;
            do { m <<= 1; e++; } while (!(m & 0x400u));
            bits = (sgn << 31) | ((uint32_t)(127 - 15 - e) << 23) | ((m & 0x3ffu) << 13);
        }
    } else if (exp == 31) {
        bits = (sgn << 31) | 0x7f800000u | (man << 13);
    } else {
        bits = (sgn << 31) | ((exp - 15u + 127u) << 23) | (man << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* LF2_ENGINE_GBUF=1: read the G-buffer back once and say what distances are actually in it.
 *
 * WHY A READBACK AND NOT A COUNTER. A counter can only say the engine was HANDED a distance; it
 * cannot say the distance survived the vertex format, the attachment, the half-float encoding
 * and the blend state. Every one of those fails silently into a buffer full of zeros, and a
 * depth of field reading zeros does nothing at all -- which looks exactly like a feature that
 * was never switched on.
 *
 * It prints the DISTINCT distances with their pixel counts, so the answer can be checked against
 * the stage's own bg.dat: a layer's depth is (stage_width-794)/(span-794) (C031), and those are
 * the numbers that must appear. A histogram nobody can check against an independent source is
 * just a number.
 */
static void gbuf_report(int w, int h)
{
    if (!getenv("LF2_ENGINE_GBUF") || !tex_gbuf) return;
    /* WHEN TO LOOK, and the first cut of this got it wrong in the way the rule about capping the
     * BORING case warns about: it latched on the first frame, which is the front-end menu -- no
     * stage, no layers, no distances -- and reported an entirely zero buffer. That reading was
     * true and useless, and it would have read as "the G-buffer does not work".
     *
     * So it retries: every 60th frame until one actually carries a distance, bounded, and if
     * none ever does the bound is reported rather than the run going quiet. The INTERESTING case
     * is the one to keep looking for; the boring one is what gets capped. */
    enum { GBUF_EVERY = 60, GBUF_TRIES = 40 };
    static int done, tries;
    static long seen;
    if (done) return;
    if ((seen++ % GBUF_EVERY) != 0) return;
    if (++tries > GBUF_TRIES) {
        if (tries == GBUF_TRIES + 1)
            fprintf(stderr, "engine gbuf: looked at %d frames spread over the run and NOT ONE "
                            "carried a distance. The buffer is being written and is entirely "
                            "zero -- so either no stage was ever on screen, or the depth hint "
                            "never reached a draw\n", GBUF_TRIES);
        return;
    }

    const size_t bytes = (size_t)w * (size_t)h * 8u;   /* RGBA16F */
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = (Uint32)bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(DEV, &ti);
    /* THE COLOUR TARGET IS READ BACK BESIDE IT, and that pairing is the whole point of the
     * second buffer rather than a convenience. An effect gated on this G-buffer is driven by a
     * CONJUNCTION -- it acts where a pixel is both selected by its own rule AND in the world --
     * and neither buffer alone can say whether that conjunction is ever satisfied. Measured
     * separately once, they both looked fine: a match frame had 1106 pixels over 0.75 luminance,
     * and the buffer had real distances in it, and a luminance bloom over the two still changed
     * NOTHING, because the two sets did not intersect at a single pixel. A histogram of each
     * half would have gone on agreeing with itself indefinitely. */
    SDL_GPUTransferBufferCreateInfo ci2;
    SDL_zero(ci2);
    ci2.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci2.size = (Uint32)((size_t)w * (size_t)h * 4u);   /* RGBA8 */
    SDL_GPUTransferBuffer *cb = SDL_CreateGPUTransferBuffer(DEV, &ci2);
    if (!tb || !cb) {
        fprintf(stderr, "engine gbuf: no download buffer (%s) -- READ NOTHING\n", SDL_GetError());
        if (tb) SDL_ReleaseGPUTransferBuffer(DEV, tb);
        if (cb) SDL_ReleaseGPUTransferBuffer(DEV, cb);
        return;
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = tex_gbuf; reg.w = (Uint32)w; reg.h = (Uint32)h; reg.d = 1;
    SDL_GPUTextureTransferInfo dst = { tb, 0, (Uint32)w, (Uint32)h };
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    reg.texture = tex_color;
    SDL_GPUTextureTransferInfo dstc = { cb, 0, (Uint32)w, (Uint32)h };
    SDL_DownloadFromGPUTexture(cp, &reg, &dstc);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (f) { SDL_WaitForGPUFences(DEV, true, &f, 1); SDL_ReleaseGPUFence(DEV, f); }

    const uint16_t *px = (const uint16_t *)SDL_MapGPUTransferBuffer(DEV, tb, false);
    const uint8_t  *cx = (const uint8_t  *)SDL_MapGPUTransferBuffer(DEV, cb, false);
    if (!px || !cx) {
        fprintf(stderr, "engine gbuf: the readback could not be mapped (%s) -- READ NOTHING\n",
                SDL_GetError());
        if (px) SDL_UnmapGPUTransferBuffer(DEV, tb);
        if (cx) SDL_UnmapGPUTransferBuffer(DEV, cb);
        SDL_ReleaseGPUTransferBuffer(DEV, tb);
        SDL_ReleaseGPUTransferBuffer(DEV, cb);
        return;
    }
    enum { BUCKETS = 24 };
    float val[BUCKETS];
    long  cnt[BUCKETS];
    int nb = 0;
    long normals = 0, zero = 0, total = 0;
    /* HOW MUCH OF THE FRAME'S BRIGHT SET IS ACTUALLY IN THE WORLD -- the question that killed
     * the luminance bloom (issue #63), kept because it is the question any future
     * brightness-driven effect has to answer before it is written. BRIGHT_T is a reference
     * point rather than a live threshold: 0.75 is the value such an effect would reach for, and
     * the run that mattered reported 766 pixels above it with ZERO of them carrying a distance. */
    const float BRIGHT_T = 0.75f;
    long bright = 0, bright_world = 0, bright_flat = 0;
    /* The WORLD's own luminance distribution, in twentieths, which is the half nobody measures.
     * A threshold gets picked from the whole frame's distribution and then applied to a
     * population the world gate has already removed the bright end of -- the HUD and the text
     * are most of what is bright in an LF2 frame. */
    enum { LUMB = 20 };
    long lumh[LUMB];
    memset(lumh, 0, sizeof lumh);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint16_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            const float nx = half_to_float(p[0]), ny = half_to_float(p[1]),
                        nz = half_to_float(p[2]), d = half_to_float(p[3]);
            const uint8_t *c = cx + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            const float l = (0.2126f * (float)c[0] + 0.7152f * (float)c[1]
                             + 0.0722f * (float)c[2]) / 255.0f;
            if (l > BRIGHT_T) {
                bright++;
                if (d > 0.0f) bright_world++; else bright_flat++;
            }
            if (d > 0.0f) {
                int b = (int)(l * (float)LUMB);
                if (b < 0) b = 0;
                if (b >= LUMB) b = LUMB - 1;
                lumh[b]++;
            }
            total++;
            if (nx * nx + ny * ny + nz * nz > 0.25f) normals++;
            if (!(d > 0.0f)) { zero++; continue; }
            int hit = -1;
            for (int b = 0; b < nb; b++)
                if (d > val[b] * 0.999f && d < val[b] * 1.001f) { hit = b; break; }
            if (hit >= 0) { cnt[hit]++; continue; }
            if (nb < BUCKETS) { val[nb] = d; cnt[nb] = 1; nb++; }
        }
    }
    SDL_UnmapGPUTransferBuffer(DEV, tb);
    SDL_UnmapGPUTransferBuffer(DEV, cb);
    SDL_ReleaseGPUTransferBuffer(DEV, tb);
    SDL_ReleaseGPUTransferBuffer(DEV, cb);

    /* Nothing on screen yet: say nothing and look again in sixty frames. The bound above is
     * what turns "never found one" into a message rather than into silence. */
    if (nb == 0) return;
    done = 1;

    fprintf(stderr, "engine gbuf: %dx%d, %ld px -- %ld with a real surface normal, %ld with no "
                    "distance (sprites, HUD and anything uncovered)\n",
            w, h, total, normals, zero);
    /* Stated as a conjunction with BOTH of its halves, so a zero cannot be read as "the effect
     * is broken" when it means "nothing bright is in the world". The two failure modes it
     * separates are the only two there are: bright_world == 0 with bright > 0 says the gate
     * rejects everything the threshold picks, and bright == 0 says the threshold picks nothing. */
    fprintf(stderr, "engine gbuf: over %.2f luminance: %ld px, of which %ld carry a distance "
                    "(in the world) and %ld do not (HUD, text, sprites -- gated out of any "
                    "world effect)\n", (double)BRIGHT_T, bright, bright_world, bright_flat);
    {
        /* Printed as a CUMULATIVE tail -- "how many world pixels a threshold here would select"
         * -- because that is the question being asked of it. A per-bucket count would need the
         * reader to do the sum, and the sum is the whole point. */
        long tail = 0;
        for (int b = LUMB - 1; b >= 0; b--) {
            tail += lumh[b];
            if (lumh[b] || tail)
                fprintf(stderr, "engine gbuf:   world luminance >= %.2f : %ld px (%.3f%% of the "
                                "world)\n", (double)b / (double)LUMB, tail,
                        100.0 * (double)tail / (double)(total - zero ? total - zero : 1));
        }
    }
    for (int a = 0; a < nb; a++)          /* nearest first, which is how a stage reads */
        for (int b = a + 1; b < nb; b++)
            if (val[b] < val[a]) {
                const float t = val[a]; val[a] = val[b]; val[b] = t;
                const long c = cnt[a]; cnt[a] = cnt[b]; cnt[b] = c;
            }
    for (int b = 0; b < nb; b++)
        fprintf(stderr, "engine gbuf:   distance %8.4f  %ld px%s\n", (double)val[b], cnt[b],
                nb == BUCKETS && b == nb - 1 ? "   (bucket list FULL -- more may exist)" : "");
}

/* Is the defocus running? ON by default, because a feature nobody can find is not a feature --
 * `LF2_DOF=off` is the A/B control arm, the same shape LF2_HD2D=off has for the lighting, and it
 * is what tools/e2e.sh render diffs against. */
int engine_dof_enabled(void)
{
    if (dof_on < 0) {
        const char *v = getenv("LF2_DOF");
        dof_on = (v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0)) ? 0 : 1;
    }
    return dof_on && st_dof != NULL;
}

/* Present the engine's finished frame onto `dst`, through the defocus when it is available.
 * Returns 0 if the caller should do a plain copy instead. */
int engine_present(SDL_Texture *dst, float max_radius)
{
    if (!init_ok || !wrapped) return 0;
    if (!engine_dof_enabled()) return 0;    /* nothing to do: the caller copies the frame */
    struct { float params[4], focus[4]; } u;
    u.params[0] = 1.0f / (float)tgt_w;
    u.params[1] = 1.0f / (float)tgt_h;
    u.params[2] = max_radius;
    u.params[3] = 1.0f;
    /* THE FOCUS IS THE FIGHTERS' PLANE, and it is 1.0 by derivation rather than by taste: a
     * parallax depth of 1 is the plane an object shifts with the camera at rate 1, which is
     * where the game puts every fighter (C018/C031). Focusing anywhere else would defocus the
     * one thing the player is looking at. */
    u.focus[0] = 1.0f;
    u.focus[1] = u.focus[2] = u.focus[3] = 0.0f;
    SDL_SetGPURenderStateFragmentUniforms(st_dof, 0, &u, sizeof u);

    SDL_SetRenderTarget(R, dst);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    SDL_SetGPURenderState(R, st_dof);
    SDL_SetTextureBlendMode(wrapped, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(R, wrapped, NULL, NULL);
    SDL_SetGPURenderState(R, NULL);
    stat_dof_frames++;
    return 1;
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
    fprintf(stderr, "engine: stage geometry -- %ld draw(s), %ld triangle(s), in the SAME pass "
                    "as the sprites%s\n", stat_geom_draws, stat_geom_tris,
            GPIPE ? "" : "  (NO geometry pipeline: sets are not drawn at all)");
    fprintf(stderr, "engine: defocus %s -- %ld frame(s) presented through it%s\n",
            engine_dof_enabled() ? "ON" : "off", stat_dof_frames,
            sh_dof ? "" : "  (NO defocus shader: frames are presented unblurred)");
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
    if (gvbuf)  { SDL_ReleaseGPUBuffer(DEV, gvbuf); gvbuf = NULL; }
    if (gvxfer) { SDL_ReleaseGPUTransferBuffer(DEV, gvxfer); gvxfer = NULL; }
    if (GPIPE) { SDL_ReleaseGPUGraphicsPipeline(DEV, GPIPE); GPIPE = NULL; }
    if (st_dof) { SDL_DestroyGPURenderState(st_dof); st_dof = NULL; }
    if (sh_dof) { SDL_ReleaseGPUShader(DEV, sh_dof); sh_dof = NULL; }
    if (SMP)   { SDL_ReleaseGPUSampler(DEV, SMP); SMP = NULL; }
    for (int k = 0; k < BLEND_KINDS; k++)
        if (PIPE[k]) { SDL_ReleaseGPUGraphicsPipeline(DEV, PIPE[k]); PIPE[k] = NULL; }
    vbuf_cap = 0;
}
