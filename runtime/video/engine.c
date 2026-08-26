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
#include "stagelight.h"
#include "engine_lighting.h"
#include "options.h"
#include "spritefilter.h"
#include "engine_textures.h"
#include "engine_visibility_probe.h"
#include "gpu_depth_format.h"
#include "gpu_shader_source.h"
#include "painter_depth.h"

#include "../shaders/gen/quad_vert_spv.h"
#include "../shaders/gen/quad_spv.h"
#include "../shaders/gen/mesh_vert_spv.h"
#include "../shaders/gen/mesh_spv.h"
#include "../shaders/gen/hd2d_quad_vert_spv.h"
#include "../shaders/gen/hd2d_character_spv.h"
#include "../shaders/gen/hd2d_shadow_spv.h"
#include "../shaders/gen/hd2d_light_spv.h"

#include "../shaders/gen/quad_vert_msl.h"
#include "../shaders/gen/quad_msl.h"
#include "../shaders/gen/mesh_vert_msl.h"
#include "../shaders/gen/mesh_msl.h"
#include "../shaders/gen/hd2d_quad_vert_msl.h"
#include "../shaders/gen/hd2d_character_msl.h"
#include "../shaders/gen/hd2d_shadow_msl.h"
#include "../shaders/gen/hd2d_light_msl.h"

/* ---- state ------------------------------------------------------------------------------ */

static SDL_Renderer *R;
static SDL_GPUDevice *DEV;
static SDL_GPUSampler *SMP;        /* NEAREST: guest pixel art */
static SDL_GPUSampler *SMP_LINEAR; /* LINEAR: premultiplied host tiles */
static SDL_GPUShaderFormat SHADER_FORMATS;
static SDL_GPUTextureFormat DEPTH_FORMAT;

/* THREE PIPELINES, one per blend mode, because SDL_GPU fixes the blend state at pipeline
 * creation. The alternative -- premultiplying everything on upload so one blend serves all --
 * would change what a keyed sprite's colour IS, and the byte-identity arms of
 * tools/e2e.py background compare exact pixels against the software blitter. Three pipelines
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

/* ---- the lighting chain (issues #37, #69) ----
 *
 * The engine's own shading: a character mask, a cast-shadow mask and a light pass, as real
 * SDL_GPU passes over the finished picture. This is what moved OUT of hd2d.c's SDL_Render
 * render states and INTO the engine, which is the arrangement issue #64 replaced and #69 made
 * the default. The shaders are the same committed blobs hd2d once ran; only the driver is
 * different.
 *
 * The light pass samples the picture being lit, so it needs a SECOND colour target to write
 * into -- a pass cannot read and write the same texture. */
/* The lighting chain's three extra colour targets. Created here with the pair, wrapped for
 * presentation and diagnostics, and BOUND into runtime/video/engine_lighting.c, which owns
 * everything else about the chain -- its pipelines, buffers and counters. tex_lit is the
 * shipping engine's presentation when the light pass runs. */
static SDL_GPUTexture *tex_chars, *tex_shadow, *tex_lit;
static SDL_Texture *wrapped_chars, *wrapped_shadow, *wrapped_lit;
static int light_ok; /* engine_lighting_init's answer: the chain's shaders and pipelines exist */

/* The offscreen pair. Colour is wrapped as an ordinary SDL_Texture (claim C030 -- the same
 * object, no copy and no readback) so the existing present path can put it on the screen while
 * the engine is being brought up beside the old renderer rather than in place of it. */
static SDL_GPUTexture *tex_color, *tex_depth;
static SDL_Texture *wrapped;
static int tgt_w, tgt_h;

static SDL_GPUBuffer *vbuf;
static SDL_GPUTransferBuffer *vxfer;
static int vbuf_cap;

static int init_done, init_ok;
static const char *init_why = "not attempted";
static long stat_frames, stat_quads, stat_batches, stat_dropped;

typedef struct {
    float x, y, depth, u, v, r, g, b, a;
} QuadVertex;

void engine_surface_dirty(uint32_t pixels) { engine_textures_surface_dirty(pixels); }

/* ---- setup ------------------------------------------------------------------------------- */

static SDL_GPUShader *shader_make(const unsigned char *spv, size_t spv_len, const unsigned char *msl, size_t msl_len,
                                  SDL_GPUShaderStage stage, int samplers, int uniforms, const char *what)
{
    GPUShaderSource source;
    if (!gpu_shader_source_select(SHADER_FORMATS, spv, spv_len, msl, msl_len, &source)) {
        fprintf(stderr, "engine: no shader payload matches the %s backend for %s\n", SDL_GetGPUDeviceDriver(DEV), what);
        return NULL;
    }
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code = source.code;
    info.code_size = source.code_size;
    info.entrypoint = source.entrypoint;
    info.format = source.format;
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
    b->src_color_blendfactor = (kind == BLEND_PREMUL) ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
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
        fprintf(stderr,
                "engine: the '%s' renderer has no GPU device, so the engine cannot run "
                "and the SDL_Render path draws instead.\n",
                SDL_GetRendererName(r));
        return 0;
    }
    SHADER_FORMATS = SDL_GetGPUShaderFormats(DEV);
    if (!(SHADER_FORMATS & (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL))) {
        init_why = "the GPU backend has no matching shader payload";
        fprintf(stderr,
                "engine: the %s backend accepts shader formats 0x%x, but this port "
                "ships SPIR-V and MSL.\n",
                SDL_GetGPUDeviceDriver(DEV), (unsigned)SHADER_FORMATS);
        return 0;
    }
    DEPTH_FORMAT = gpu_depth_format_select(DEV);
    if (DEPTH_FORMAT == SDL_GPU_TEXTUREFORMAT_INVALID) {
        /* Not fatal the way it is for the geometry pass -- sprites arrive painter-ordered and
         * would still draw correctly without a depth buffer. It IS fatal to the reason this
         * engine exists, so it refuses rather than becoming a slower copy of what it replaces. */
        init_why = "no supported depth target";
        fprintf(stderr,
                "engine: the %s backend has no supported depth-stencil target. Sprites "
                "would still draw, but a depth buffer shared with the stage geometry is "
                "the whole reason for this engine, so it does NOT run here.\n",
                SDL_GetGPUDeviceDriver(DEV));
        return 0;
    }

    SDL_GPUShader *vs = shader_make(quad_vert_spv, sizeof quad_vert_spv, quad_vert_msl, sizeof quad_vert_msl,
                                    SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, "quad vertex");
    SDL_GPUShader *fs = shader_make(quad_spv, sizeof quad_spv, quad_msl, sizeof quad_msl, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                    1, 1, "quad fragment");
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
    attrs[0].location = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = (Uint32)offsetof(QuadVertex, x);
    attrs[1].location = 1;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[1].offset = (Uint32)offsetof(QuadVertex, depth);
    attrs[2].location = 2;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[2].offset = (Uint32)offsetof(QuadVertex, u);
    attrs[3].location = 3;
    attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
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
        pi.target_info.depth_stencil_format = DEPTH_FORMAT;
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
        SDL_GPUShader *gvs = shader_make(mesh_vert_spv, sizeof mesh_vert_spv, mesh_vert_msl, sizeof mesh_vert_msl,
                                         SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, "geometry vertex");
        SDL_GPUShader *gfs = shader_make(mesh_spv, sizeof mesh_spv, mesh_msl, sizeof mesh_msl,
                                         SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, "geometry fragment");
        if (gvs && gfs) {
            SDL_GPUVertexBufferDescription gvbd;
            SDL_zero(gvbd);
            gvbd.slot = 0;
            gvbd.pitch = sizeof(MeshVertex);
            gvbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            SDL_GPUVertexAttribute ga[4];
            SDL_zero(ga);
            ga[0].location = 0;
            ga[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            ga[0].offset = (Uint32)offsetof(MeshVertex, x);
            ga[1].location = 1;
            ga[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            ga[1].offset = (Uint32)offsetof(MeshVertex, u);
            ga[2].location = 2;
            ga[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            ga[2].offset = (Uint32)offsetof(MeshVertex, nx);
            ga[3].location = 3;
            ga[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            ga[3].offset = (Uint32)offsetof(MeshVertex, r);

            SDL_GPUColorTargetDescription gct;
            SDL_zero(gct);
            gct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            blend_state(&gct.blend_state, BLEND_ALPHA);

            SDL_GPUGraphicsPipelineCreateInfo gp;
            SDL_zero(gp);
            gp.vertex_shader = gvs;
            gp.fragment_shader = gfs;
            gp.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            gp.vertex_input_state.num_vertex_buffers = 1;
            gp.vertex_input_state.vertex_buffer_descriptions = &gvbd;
            gp.vertex_input_state.num_vertex_attributes = 4;
            gp.vertex_input_state.vertex_attributes = ga;
            gp.target_info.num_color_targets = 1;
            gp.target_info.color_target_descriptions = &gct;
            gp.target_info.has_depth_stencil_target = true;
            gp.target_info.depth_stencil_format = DEPTH_FORMAT;
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
                fprintf(stderr,
                        "engine: the geometry pipeline failed: %s -- sprites draw, "
                        "hand-woven sets do NOT\n",
                        SDL_GetError());
        }
        if (gvs) SDL_ReleaseGPUShader(DEV, gvs);
        if (gfs) SDL_ReleaseGPUShader(DEV, gfs);
    }

    /* ---- the lighting chain (issues #37, #64, #69), now its own file ---- */
    light_ok = engine_lighting_init(DEV, SHADER_FORMATS, DEPTH_FORMAT);

    SDL_GPUSamplerCreateInfo si;
    SDL_zero(si);
    /* NEAREST, always: this is pixel art magnified two or three times, and the frame is built
     * at the window's resolution so a sprite is resampled exactly once. */
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SMP = SDL_CreateGPUSampler(DEV, &si);

    /* The same shape as SMP but filtering: host tiles are outline-font and SVG coverage
     * rasterised at output scale, and nearest would quantise that coverage again at
     * fractional DPI. */
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    SMP_LINEAR = SDL_CreateGPUSampler(DEV, &si);
    if (!SMP || !SMP_LINEAR || engine_lighting_sampler_missing()) {
        init_why = "no sampler";
        return 0;
    }

    engine_textures_init(DEV);
    init_ok = 1;
    init_why = "ready";
    GPUShaderSource selected;
    (void)gpu_shader_source_select(SHADER_FORMATS, quad_vert_spv, sizeof quad_vert_spv, quad_vert_msl,
                                   sizeof quad_vert_msl, &selected);
    fprintf(stderr,
            "engine: up on the %s backend with %s shaders, sharing the renderer's "
            "device (%s depth, one texture pool)\n",
            SDL_GetGPUDeviceDriver(DEV), gpu_shader_format_name(selected.format), gpu_depth_format_name(DEPTH_FORMAT));
    engine_visibility_probe_run(R);
    return 1;
}

int engine_ready(void) { return init_ok; }

/* Which renderer draws. The pause menu's option owns it (issue #69); LF2_ENGINE is only the
 * startup pin, and a route that must pin the classic path sets it to 0. It is an A/B control
 * arm, not a feature switch: the engine is the default, and the old path stays selectable so
 * the two can go on being diffed -- exactly as LF2_BG_ORIG does for the background override. A
 * reimplementation that cannot be diffed against what it replaces is a rewrite. */
int engine_enabled(void) { return opt_renderer_engine() && init_ok; }

/* ---- targets and buffers ------------------------------------------------------------------ */

static void targets_release(void)
{
    if (wrapped) {
        SDL_DestroyTexture(wrapped);
        wrapped = NULL;
    }
    if (wrapped_chars) {
        SDL_DestroyTexture(wrapped_chars);
        wrapped_chars = NULL;
    }
    if (wrapped_shadow) {
        SDL_DestroyTexture(wrapped_shadow);
        wrapped_shadow = NULL;
    }
    if (wrapped_lit) {
        SDL_DestroyTexture(wrapped_lit);
        wrapped_lit = NULL;
    }
    if (tex_color) {
        SDL_ReleaseGPUTexture(DEV, tex_color);
        tex_color = NULL;
    }
    if (tex_depth) {
        SDL_ReleaseGPUTexture(DEV, tex_depth);
        tex_depth = NULL;
    }
    if (tex_chars) {
        SDL_ReleaseGPUTexture(DEV, tex_chars);
        tex_chars = NULL;
    }
    if (tex_shadow) {
        SDL_ReleaseGPUTexture(DEV, tex_shadow);
        tex_shadow = NULL;
    }
    if (tex_lit) {
        SDL_ReleaseGPUTexture(DEV, tex_lit);
        tex_lit = NULL;
    }
    tgt_w = tgt_h = 0;
}

static int targets_make(int w, int h)
{
    if (tex_color && tgt_w == w && tgt_h == h) return 1;
    targets_release();

    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_color = SDL_CreateGPUTexture(DEV, &ci);

    ci.format = DEPTH_FORMAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tex_depth = SDL_CreateGPUTexture(DEV, &ci);

    /* The lighting chain's three extra targets: the character mask, the cast-shadow mask and
     * the lit picture. All are full resolution and sampleable by the following pass. */
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_chars = SDL_CreateGPUTexture(DEV, &ci);
    tex_shadow = SDL_CreateGPUTexture(DEV, &ci);
    tex_lit = SDL_CreateGPUTexture(DEV, &ci);

    if (!tex_color || !tex_depth || !tex_chars || !tex_shadow || !tex_lit) {
        fprintf(stderr, "engine: could not allocate the %dx%d target pair: %s\n", w, h, SDL_GetError());
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
    /* The masks and lit frame are wrapped for the SHOW diagnostics. The light pass samples
     * the masks directly; they otherwise never leave the engine. */
    {
        struct {
            SDL_GPUTexture *gpu;
            SDL_Texture **wrap;
            const char *what;
            int required;
        } w3[3] = {
            {tex_chars, &wrapped_chars, "character mask", 0},
            {tex_shadow, &wrapped_shadow, "cast-shadow mask", 0},
            /* Unlike the two diagnostic masks, this is the shipping engine's presentation.
             * Returning success with no wrapper makes a working light pass draw nothing. */
            {tex_lit, &wrapped_lit, "lit frame", 1},
        };
        for (int i = 0; i < 3; i++) {
            SDL_PropertiesID p = SDL_CreateProperties();
            SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, w3[i].gpu);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
            *w3[i].wrap = SDL_CreateTextureWithProperties(R, p);
            SDL_DestroyProperties(p);
            if (!*w3[i].wrap) {
                fprintf(stderr,
                        "engine: the %s could not be wrapped as a renderer texture "
                        "(%s) -- it cannot be shown%s\n",
                        w3[i].what, SDL_GetError(), w3[i].required ? ", so the engine target is refused" : "");
                if (w3[i].required) {
                    targets_release();
                    return 0;
                }
            } else {
                SDL_SetTextureScaleMode(*w3[i].wrap, SDL_SCALEMODE_NEAREST);
            }
        }
    }
    engine_lighting_bind_targets(tex_color, tex_depth, tex_chars, tex_shadow, tex_lit, SMP);
    tgt_w = w;
    tgt_h = h;
    return 1;
}

static int vbuf_reserve(int bytes)
{
    if (vbuf && vbuf_cap >= bytes) return 1;
    if (vbuf) {
        SDL_ReleaseGPUBuffer(DEV, vbuf);
        vbuf = NULL;
    }
    if (vxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, vxfer);
        vxfer = NULL;
    }

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
        fprintf(stderr, "engine: could not allocate a %d-byte vertex buffer: %s\n", bytes, SDL_GetError());
        if (vbuf) {
            SDL_ReleaseGPUBuffer(DEV, vbuf);
            vbuf = NULL;
        }
        if (vxfer) {
            SDL_ReleaseGPUTransferBuffer(DEV, vxfer);
            vxfer = NULL;
        }
        vbuf_cap = 0;
        return 0;
    }
    vbuf_cap = bytes;
    return 1;
}

/* One gap's geometry, uploaded into its own buffer. Separate from the quad buffer because the
 * vertex FORMAT differs -- a MeshVertex carries four position channels and a normal. */
static int gvbuf_reserve(int bytes)
{
    if (gvbuf && gvbuf_cap >= bytes) return 1;
    if (gvbuf) {
        SDL_ReleaseGPUBuffer(DEV, gvbuf);
        gvbuf = NULL;
    }
    if (gvxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, gvxfer);
        gvxfer = NULL;
    }
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
        fprintf(stderr, "engine: could not allocate a %d-byte geometry buffer: %s\n", bytes, SDL_GetError());
        if (gvbuf) {
            SDL_ReleaseGPUBuffer(DEV, gvbuf);
            gvbuf = NULL;
        }
        if (gvxfer) {
            SDL_ReleaseGPUTransferBuffer(DEV, gvxfer);
            gvxfer = NULL;
        }
        gvbuf_cap = 0;
        return 0;
    }
    gvbuf_cap = bytes;
    return 1;
}

/* The fragment uniform quad.frag declares: f_data, f_rect, f_pass[SPRITE_PASS_MAX],
 * f_outline, f_cut -- four floats each, and the pass array is the chain's own cap. */
enum { QUAD_UNIFORM_PASS0 = 8, QUAD_UNIFORM_FLOATS = QUAD_UNIFORM_PASS0 + 4 * SPRITE_PASS_MAX + 8 };

/* ---- the draw ----------------------------------------------------------------------------- */

/* The object-sprite sampling chain (issue #112), read per frame so a menu change is live. The
 * policy -- what parses, the own-draw rule, the AUTO factor, the outline's margin -- is
 * spritefilter.h, walked by ctest. */
static SpriteChain sprite_chain;

static void sprite_filter_begin_frame(void) { sprite_chain = *opt_sprite_chain(); }

/* The AUTO factor of one quad: its magnification, which is the view's world scale measured
 * where it applies. Zero spans (a degenerate quad) fall back to 1 inside the helper. */
static float quad_auto_factor(const EngineQuad *q)
{ return spritechain_auto_factor(q->source_w, q->source_h, q->w, q->h); }

/* An outline lives OUTSIDE the art, so the quad that carries one has to be bigger than the
 * frame: the geometry and its uv both grow by the outline's width, converted from chain pixels
 * to source texels and then to destination pixels. The uv rect the shader clamps against stays
 * the ORIGINAL frame, which is what makes the new margin read as empty rather than as a smear
 * of the border texel. Nothing moves for a quad with no outline. */
static void quad_grow_for_outline(EngineQuad *e)
{
    const float m = spritechain_margin_texels(&sprite_chain, quad_auto_factor(e));
    if (m <= 0.0f || e->sw <= 0 || e->sh <= 0) return;
    const float span_w = e->source_w;
    const float span_h = e->source_h;
    if (span_w <= 0.0f || span_h <= 0.0f) return;
    const float ex = m * e->w / span_w, ey = m * e->h / span_h;
    const float du = m / (float)e->sw, dv = m / (float)e->sh;
    e->x -= ex;
    e->y -= ey;
    e->w += 2.0f * ex;
    e->h += 2.0f * ey;
    const float su = e->u0 <= e->u1 ? 1.0f : -1.0f, sv = e->v0 <= e->v1 ? 1.0f : -1.0f;
    e->u0 -= su * du;
    e->u1 += su * du;
    e->v0 -= sv * dv;
    e->v1 += sv * dv;
}

/* The fragment uniform a filtered object sprite carries: the chain, the quad's authoritative
 * source rectangle in sheet texels, and the outline. Integer source bounds are passed directly:
 * texel-centre UV endpoints span extent-1 and cannot be inverted back into a rectangle. */
static void sprite_uniform(const EngineQuad *q, float *flags)
{
    const float autof = quad_auto_factor(q);
    flags[0] = 1.0f;
    flags[1] = (float)sprite_chain.count;
    flags[2] = (float)sprite_chain.smooth;
    flags[3] = (float)sprite_chain.outline;
    flags[4] = q->source_x;
    flags[5] = q->source_y;
    flags[6] = q->source_w;
    flags[7] = q->source_h;
    for (int p = 0; p < sprite_chain.count; p++) {
        flags[QUAD_UNIFORM_PASS0 + p * 4] = spritechain_pass_factor(&sprite_chain.pass[p], autof);
        flags[QUAD_UNIFORM_PASS0 + p * 4 + 1] = (float)sprite_chain.pass[p].kind;
    }
    const int outline_at = QUAD_UNIFORM_PASS0 + 4 * SPRITE_PASS_MAX;
    flags[outline_at + 0] = sprite_chain.outline_rgb[0];
    flags[outline_at + 1] = sprite_chain.outline_rgb[1];
    flags[outline_at + 2] = sprite_chain.outline_rgb[2];
    flags[outline_at + 3] = 1.0f;
    flags[outline_at + 4] = (float)spritechain_linear_cut(&sprite_chain);
    flags[outline_at + 5] = (float)sprite_chain.inner;
}

static void emit(QuadVertex *v, const EngineQuad *q, float depth)
{
    const float x0 = q->x, y0 = q->y, x1 = q->x + q->w, y1 = q->y + q->h;
    const QuadVertex a = {x0, y0, depth, q->u0, q->v0, q->r, q->g, q->b, q->a};
    const QuadVertex b = {x1, y0, depth, q->u1, q->v0, q->r, q->g, q->b, q->a};
    const QuadVertex c = {x1, y1, depth, q->u1, q->v1, q->r, q->g, q->b, q->a};
    const QuadVertex d = {x0, y1, depth, q->u0, q->v1, q->r, q->g, q->b, q->a};
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = a;
    v[4] = c;
    v[5] = d;
}

SDL_Texture *engine_draw(const EngineQuad *q, int n, const EngineGeom *g, int ng, int w, int h)
{
    if (!init_ok || n <= 0 || w <= 0 || h <= 0) return NULL;
    if (!targets_make(w, h)) return NULL;
    engine_textures_begin_frame();
    sprite_filter_begin_frame();
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
    for (int i = 0; i < n; i++) {
        EngineQuad e = q[i];
        if (spritechain_needs_own_draw(&sprite_chain, e.is_object, e.host_argb != NULL)) { quad_grow_for_outline(&e); }
        emit(vp + (size_t)i * 6, &e, painter_depth(i, n));
    }
    SDL_UnmapGPUTransferBuffer(DEV, vxfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    if (!cmd) {
        fprintf(stderr, "engine: no command buffer: %s\n", SDL_GetError());
        return NULL;
    }

    /* The geometry, concatenated into one buffer with each piece's offset remembered, so the
     * whole frame is a single upload however many gaps a set occupies. */
    int gtotal = 0;
    for (int k = 0; k < ng; k++)
        if (GPIPE && g[k].v && g[k].n > 0) gtotal += g[k].n;
    int goff[64];
    int gused = 0;
    if (gtotal > 0 && gvbuf_reserve(gtotal * (int)sizeof(MeshVertex))) {
        void *gmap = SDL_MapGPUTransferBuffer(DEV, gvxfer, false);
        if (gmap) {
            MeshVertex *gp = (MeshVertex *)gmap;
            int at = 0;
            for (int k = 0; k < ng && gused < (int)(sizeof goff / sizeof goff[0]); k++) {
                if (!GPIPE || !g[k].v || g[k].n <= 0) {
                    goff[k] = -1;
                    continue;
                }
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
    SDL_GPUTransferBufferLocation from = {vxfer, 0};
    SDL_GPUBufferRegion into = {vbuf, 0, (Uint32)(verts * (int)sizeof(QuadVertex))};
    SDL_UploadToGPUBuffer(copy, &from, &into, false);
    if (gtotal > 0) {
        SDL_GPUTransferBufferLocation gfrom = {gvxfer, 0};
        SDL_GPUBufferRegion ginto = {gvbuf, 0, (Uint32)(gtotal * (int)sizeof(MeshVertex))};
        SDL_UploadToGPUBuffer(copy, &gfrom, &ginto, false);
    }
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = tex_color;
    /* Opaque black, not transparent: this is the whole frame, not an overlay over one. A
     * transparent clear would let whatever the present path had behind it show through the
     * columns no quad covers, which is issue #29's ghost by another route. */
    cti.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = tex_depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    /* Store the final visible painter depth. Geometry uses it while sharing this pass, then the
     * character-mask pass reloads it as a depth attachment to reject silhouettes covered by
     * later solid painters. It needs DEPTH_STENCIL_TARGET usage only: both consumers use fixed
     * depth testing, not shader sampling. */
    dti.store_op = SDL_GPU_STOREOP_STORE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &dti);
    if (!pass) {
        fprintf(stderr, "engine: the render pass could not begin: %s\n", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return NULL;
    }
    SDL_GPUBufferBinding vb = {vbuf, 0};
    const float view[4] = {(float)w, (float)h, 0.0f, 0.0f};

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
            SDL_GPUBufferBinding gvb = {gvbuf, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &gvb, 1);
            SDL_GPUTextureSamplerBinding gts = {tex_color, SMP};
            SDL_BindGPUFragmentSamplers(pass, 0, &gts, 1);
            const float cam[12] = {
                (float)g[k].camera, 0.0f,         0.0f, 0.0f, g[k].sx_scale, g[k].sx_bias,
                g[k].sy_scale,      g[k].sy_bias, lo,   hi,   0.0f,          0.0f,
            };
            const float material[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof cam);
            SDL_PushGPUFragmentUniformData(cmd, 0, material, sizeof material);
            SDL_DrawGPUPrimitives(pass, (Uint32)g[k].n, 1, (Uint32)goff[k], 0);
            stat_geom_draws++;
            stat_geom_tris += g[k].n / 3;
            bound_pipe = -1; /* the quad pipeline must be rebound after this */
        }

        SDL_GPUTexture *t = (q[i].src_pixels || q[i].host_argb) ? engine_texture_for(&q[i]) : NULL;
        const int kind = q[i].blend < 0 || q[i].blend >= BLEND_KINDS ? BLEND_ALPHA : q[i].blend;
        /* #112: a filtered object sprite carries its own uv rect, so it never batches */
        const int own = t && spritechain_needs_own_draw(&sprite_chain, q[i].is_object, q[i].host_argb != NULL);
        int j = i + 1;
        while (j < n && j - i < 4096) {
            if (own) break; /* a filtered object sprite is always its own draw */
            const int k2 = q[j].blend < 0 || q[j].blend >= BLEND_KINDS ? BLEND_ALPHA : q[j].blend;
            if (k2 != kind) break;
            SDL_GPUTexture *t2 = (q[j].src_pixels || q[j].host_argb) ? engine_texture_for(&q[j]) : NULL;
            if (t2 != t) break;
            j++;
        }
        if (!t && (q[i].src_pixels || q[i].host_argb)) {
            stat_dropped += j - i;
            i = j;
            continue;
        }

        if (bound_pipe != kind) {
            SDL_BindGPUGraphicsPipeline(pass, PIPE[kind]);
            bound_pipe = kind;
        }
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        /* A sampler must be bound whether or not there is art: the fragment shader declares
         * one, and leaving it unbound is undefined rather than "the branch is not taken". The
         * colour target stands in when there is nothing to sample -- u_flags.x is 0, so nothing
         * reads it. */
        /* Guest art is authored pixel art and stays nearest. Host tiles are outline-font and
         * SVG coverage rasterised at output scale; nearest would quantise that coverage again
         * at fractional DPI and is why the supposedly high-resolution text still looked like
         * the original bitmap font. */
        SDL_GPUTextureSamplerBinding tsb = {t ? t : tex_color, t && q[i].host_argb ? SMP_LINEAR : SMP};
        if (bound_tex != tsb.texture) { bound_tex = tsb.texture; }
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        /* The sampling uniform is per DRAW because the chain clamps its taps into the quad's
         * authoritative source bounds -- a sheet's frames butt edge to edge, so those bounds
         * cannot be batch-level state. Only object sprites carry a chain; everything else
         * takes the shader's original single-tap path (issue #112). */
        float flags[QUAD_UNIFORM_FLOATS] = {t ? 1.0f : 0.0f};
        if (own) sprite_uniform(&q[i], flags);
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
        SDL_GPUBufferBinding gvb = {gvbuf, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &gvb, 1);
        SDL_GPUTextureSamplerBinding gts = {tex_color, SMP};
        SDL_BindGPUFragmentSamplers(pass, 0, &gts, 1);
        const float cam[12] = {
            (float)g[k].camera, 0.0f,         0.0f, 0.0f, g[k].sx_scale, g[k].sx_bias,
            g[k].sy_scale,      g[k].sy_bias, lo,   hi,   0.0f,          0.0f,
        };
        const float material[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof cam);
        SDL_PushGPUFragmentUniformData(cmd, 0, material, sizeof material);
        SDL_DrawGPUPrimitives(pass, (Uint32)g[k].n, 1, (Uint32)goff[k], 0);
        stat_geom_draws++;
        stat_geom_tris += g[k].n / 3;
    }

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    stat_frames++;
    stat_quads += n;
    SDL_GPUTexture *finished = engine_lighting_run(q, n, w, h);
    return finished == tex_color    ? wrapped
           : finished == tex_chars  ? wrapped_chars
           : finished == tex_shadow ? wrapped_shadow
                                    : wrapped_lit;
}

void engine_report(void)
{
    if (!getenv("LF2_ENGINE_DEBUG")) return;
    fprintf(stderr,
            "engine: %s (%s). %ld frame(s), %ld quad(s) in %ld batch(es), "
            "%ld quad(s) DROPPED\n",
            engine_enabled() ? "DRAWING" : "not drawing", init_why, stat_frames, stat_quads, stat_batches,
            stat_dropped);
    fprintf(stderr, "engine: render targets are %dx%d output pixels\n", tgt_w, tgt_h);
    engine_textures_report();
    /* The zero is printed and named, because zero is the ordinary answer when the engine is
     * built but not selected -- and "built but not selected" must not look like "selected and
     * drew nothing". */
    if (engine_enabled() && !stat_frames)
        fprintf(stderr, "engine: it is SELECTED and has drawn NOTHING -- no frame reached it, "
                        "which is a different fault from a frame that came out wrong\n");
    fprintf(stderr,
            "engine: stage geometry -- %ld draw(s), %ld triangle(s), in the SAME pass "
            "as the sprites%s\n",
            stat_geom_draws, stat_geom_tris, GPIPE ? "" : "  (NO geometry pipeline: sets are not drawn at all)");
    engine_lighting_report();
    if (stat_dropped)
        fprintf(stderr,
                "engine: %ld quad(s) were dropped for want of a texture; art is MISSING "
                "from those frames\n",
                stat_dropped);
}

void engine_shutdown(void)
{
    if (!DEV) return;
    engine_textures_shutdown();
    targets_release();
    if (vbuf) {
        SDL_ReleaseGPUBuffer(DEV, vbuf);
        vbuf = NULL;
    }
    if (vxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, vxfer);
        vxfer = NULL;
    }
    if (gvbuf) {
        SDL_ReleaseGPUBuffer(DEV, gvbuf);
        gvbuf = NULL;
    }
    if (gvxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, gvxfer);
        gvxfer = NULL;
    }
    if (GPIPE) {
        SDL_ReleaseGPUGraphicsPipeline(DEV, GPIPE);
        GPIPE = NULL;
    }
    engine_lighting_shutdown();
    if (SMP) {
        SDL_ReleaseGPUSampler(DEV, SMP);
        SMP = NULL;
    }
    if (SMP_LINEAR) {
        SDL_ReleaseGPUSampler(DEV, SMP_LINEAR);
        SMP_LINEAR = NULL;
    }
    for (int k = 0; k < BLEND_KINDS; k++)
        if (PIPE[k]) {
            SDL_ReleaseGPUGraphicsPipeline(DEV, PIPE[k]);
            PIPE[k] = NULL;
        }
    vbuf_cap = 0;
}
