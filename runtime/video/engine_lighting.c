/* The lighting chain of the engine: the character mask, the cast-shadow mask and the light
 * pass, as SDL_GPU passes over the finished picture (issues #37, #64, #69).
 *
 * WHY THIS IS ITS OWN FILE. The chain is one subsystem with its own shaders, pipelines,
 * sampler and vertex buffer; it lived inside engine.c and pushed that file over its line
 * budget. What it does NOT own is the target set it draws into -- the masks' textures are
 * created by engine.c's targets_make and bound here -- nor the quad pass that draws the
 * picture in the first place. The split is what DRAWS the frame (engine.c) versus what
 * RE-LIGHTS the finished frame (this file).
 *
 * The three passes draw from ONE LightVertex buffer:
 *
 *   CHARS   the object quads again, through hd2d_character.frag, into a mask that says which
 *           pixels are a fighter and how high off the ground each is.
 *   SHADOW  the same objects' SHEARED silhouettes, through hd2d_shadow.frag, into the mask
 *           the light is taken away through.
 *   LIGHT   a full-screen pass through hd2d_light.frag that re-lights the picture from the
 *           one key light (hd2d.c), using the two masks, into the bound lit target.
 *
 * The shaders were written for SDL_Render's varying convention (colour at location 0, uv at
 * location 1) and speak a different vertex format from the sprite pipeline's -- see
 * hd2d_quad.vert. Character visibility and projected shadow reception both depth-test against
 * the completed scene.
 */
#include "lf2_log.h"
#include "environment.h"
#include "engine_lighting.h"

#include <SDL3/SDL_gpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_depth_format.h"
#include "gpu_shader_source.h"
#include "hd2d.h"
#include "engine_textures.h"
#include "options.h"
#include "painter_depth.h"
#include "stagelight.h"

#include "../shaders/gen/hd2d_quad_vert_spv.h"
#include "../shaders/gen/hd2d_character_spv.h"
#include "../shaders/gen/hd2d_shadow_spv.h"
#include "../shaders/gen/hd2d_light_spv.h"
#include "../shaders/gen/hd2d_quad_vert_msl.h"
#include "../shaders/gen/hd2d_character_msl.h"
#include "../shaders/gen/hd2d_shadow_msl.h"
#include "../shaders/gen/hd2d_light_msl.h"

static SDL_GPUDevice *DEV;
static SDL_GPUShaderFormat FORMATS;
static SDL_GPUGraphicsPipeline *p_chars, *p_shadow, *p_light;
static SDL_GPUSampler *smp_linear;

/* The bound target set. Owned by engine.c's targets_make; bound here. */
static SDL_GPUTexture *tex_color_bound, *tex_depth_bound, *tex_chars, *tex_shadow, *tex_lit;
static SDL_GPUSampler *smp_nearest;

/* The mask passes need a vertex format of their own (pos2, depth1, uv2, colour4 -- the hd2d
 * shaders were written for SDL_Render's varying convention, see hd2d_quad.vert), so they draw
 * from their own buffer rather than from the sprite quad buffer. */
typedef struct {
    float x, y, depth, u, v, r, g, b, a;
} LightVertex;

static SDL_GPUBuffer *lvbuf;
static SDL_GPUTransferBuffer *lvxfer;
static int lvbuf_cap;
static int light_ok; /* the light chain's shaders and pipelines exist */
static long stat_light_frames, stat_char_quads, stat_shadow_quads;

/* LINEAR for the light pass's reads of the two masks, unlike the sprite sampler: the light
 * pass is not a pixel-perfect blit but a per-pixel query at slightly-offset texels (the bevel
 * ring, the feather), and stepping those over NEAREST would read the jagged edge the pass
 * exists to soften. */
static SDL_GPUSampler *linear_sampler(SDL_GPUDevice *dev)
{
    SDL_GPUSamplerCreateInfo si;
    SDL_zero(si);
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(dev, &si);
}

int engine_lighting_init(SDL_GPUDevice *dev, SDL_GPUShaderFormat formats, SDL_GPUTextureFormat depth_format)
{
    DEV = dev;
    FORMATS = formats;

    /* The hd2d fragment shaders were written for SDL_Render's varying convention (colour at
     * location 0, uv at location 1) and speak a different vertex format from the sprite
     * pipeline's -- hd2d_quad.vert explains. Character visibility and projected shadow
     * reception both depth-test against the completed scene. */
    SDL_GPUShader *vert =
        gpu_shader_make(DEV, FORMATS, hd2d_quad_vert_spv, sizeof hd2d_quad_vert_spv, hd2d_quad_vert_msl,
                        sizeof hd2d_quad_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, "lighting vertex");

    SDL_GPUVertexBufferDescription lvbd;
    SDL_zero(lvbd);
    lvbd.slot = 0;
    lvbd.pitch = sizeof(LightVertex);
    lvbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute la[4];
    SDL_zero(la);
    la[0].location = 0;
    la[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    la[0].offset = (Uint32)offsetof(LightVertex, x);
    la[1].location = 1;
    la[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    la[1].offset = (Uint32)offsetof(LightVertex, depth);
    la[2].location = 2;
    la[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    la[2].offset = (Uint32)offsetof(LightVertex, u);
    la[3].location = 3;
    la[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    la[3].offset = (Uint32)offsetof(LightVertex, r);

    SDL_GPUColorTargetDescription lct;
    SDL_zero(lct);
    lct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    /* NONE on the masks and the lit frame alike: the mask passes write every pixel they test
     * and clear the rest, and the light pass owns tex_lit outright. There is nothing behind
     * any of them to blend with. */

    if (vert) {
        SDL_GPUGraphicsPipelineCreateInfo lp;
        SDL_zero(lp);
        lp.vertex_shader = vert;
        lp.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        lp.vertex_input_state.num_vertex_buffers = 1;
        lp.vertex_input_state.vertex_buffer_descriptions = &lvbd;
        lp.vertex_input_state.num_vertex_attributes = 4;
        lp.vertex_input_state.vertex_attributes = la;
        lp.target_info.num_color_targets = 1;
        lp.target_info.color_target_descriptions = &lct;
        lp.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        lp.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

        lp.fragment_shader =
            gpu_shader_make(DEV, FORMATS, hd2d_character_spv, sizeof hd2d_character_spv, hd2d_character_msl,
                            sizeof hd2d_character_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, "character mask");
        lp.target_info.has_depth_stencil_target = true;
        lp.target_info.depth_stencil_format = depth_format;
        lp.depth_stencil_state.enable_depth_test = true;
        lp.depth_stencil_state.enable_depth_write = false;
        lp.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        p_chars = SDL_CreateGPUGraphicsPipeline(DEV, &lp);
        if (!p_chars) {
            lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                           "engine: the character-mask pipeline failed: %s -- no lighting chain\n", SDL_GetError());
        }
        SDL_ReleaseGPUShader(DEV, lp.fragment_shader);

        /* A projected shadow has the caster's painter depth. Strict LESS admits the
         * earlier-painted floor, rejects the caster itself at equal depth, and rejects a later
         * foreground object (#99). The visibility probe's named LEQUAL arm is the deliberate
         * other-answer mutation: it must expose self-shadowing on the same shipping pipeline
         * or the equal-depth sample is not a trustworthy instrument. */
        const char *visibility_probe = lf2_environment_get(LF2_ENV_VISIBILITY_PROBE);
        lp.depth_stencil_state.compare_op = visibility_probe && strcmp(visibility_probe, "shadow-self-lequal") == 0
                                                ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL
                                                : SDL_GPU_COMPAREOP_LESS;
        lp.fragment_shader =
            gpu_shader_make(DEV, FORMATS, hd2d_shadow_spv, sizeof hd2d_shadow_spv, hd2d_shadow_msl,
                            sizeof hd2d_shadow_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, "cast-shadow mask");
        p_shadow = SDL_CreateGPUGraphicsPipeline(DEV, &lp);
        if (!p_shadow) {
            lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                           "engine: the cast-shadow-mask pipeline failed: %s -- no lighting chain\n", SDL_GetError());
        }
        SDL_ReleaseGPUShader(DEV, lp.fragment_shader);

        lp.target_info.has_depth_stencil_target = false;
        SDL_zero(lp.depth_stencil_state);
        lp.fragment_shader = gpu_shader_make(DEV, FORMATS, hd2d_light_spv, sizeof hd2d_light_spv, hd2d_light_msl,
                                             sizeof hd2d_light_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1, "light");
        p_light = SDL_CreateGPUGraphicsPipeline(DEV, &lp);
        if (!p_light) {
            lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                           "engine: the light pipeline failed: %s -- no lighting chain\n", SDL_GetError());
        }
        SDL_ReleaseGPUShader(DEV, lp.fragment_shader);
    }

    smp_linear = linear_sampler(DEV);

    /* AFTER the sampler, not before: light_ok used to be computed while smp_linear was still
     * NULL, so it was always 0 and the whole chain -- character mask, cast-shadow mask, light
     * pass -- reported "did not come up" and never ran on any backend. A frame is then
     * presented unlit with the game's own ellipse deleted, which reads as a fighter with no
     * shadow rather than as a broken effect (issue #72's picture). */
    light_ok = vert && p_chars && p_shadow && p_light && smp_linear;
    if (vert) SDL_ReleaseGPUShader(DEV, vert);

    if (!light_ok || !smp_linear)
        lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                       "engine: the lighting chain did not come up -- frames are presented "
                       "unlit, which is a picture without shading rather than a broken one\n");
    return light_ok;
}

/* True when the chain could not even build its own sampler, which the engine treats as fatal
 * rather than as "unlit": the old single-file arrangement refused there and must keep doing
 * so, or a half-working device would silently change how much of the port runs. */
int engine_lighting_sampler_missing(void)
{
    return smp_linear == NULL;
}

void engine_lighting_bind_targets(SDL_GPUTexture *color, SDL_GPUTexture *depth, SDL_GPUTexture *chars,
                                  SDL_GPUTexture *shadow, SDL_GPUTexture *lit, SDL_GPUSampler *nearest)
{
    tex_color_bound = color;
    tex_depth_bound = depth;
    tex_chars = chars;
    tex_shadow = shadow;
    tex_lit = lit;
    smp_nearest = nearest;
}

/* The sprite quad, in the mask pass's own vertex format. The shader only alpha-tests the
 * texture, so the tint is white and the quad is the object's own rect. */
static void light_emit_char(LightVertex *v, const EngineQuad *q, float depth)
{
    const LightVertex a = {q->x, q->y, depth, q->u0, q->v0, 1, 1, 1, 1};
    const LightVertex b = {q->x + q->w, q->y, depth, q->u1, q->v0, 1, 1, 1, 1};
    const LightVertex c = {q->x + q->w, q->y + q->h, depth, q->u1, q->v1, 1, 1, 1, 1};
    const LightVertex d = {q->x, q->y + q->h, depth, q->u0, q->v1, 1, 1, 1, 1};
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = a;
    v[4] = c;
    v[5] = d;
}

/* The same sprite laid down as its cast shadow through the shared projection. The object's z
 * row is the floor; every source corner keeps its actual x and derives its height from that
 * row. Recentring the whole source rectangle on the ellipse discarded LF2's per-frame authored
 * offset and moved the visible feet sideways (#97). */
static void light_emit_shadow(LightVertex *v, const EngineQuad *q, float depth, float across, float up)
{
    float o[8];
    stagelight_shadow_quad(across, up, q->x, q->y, q->w, q->h, q->ground_gy, o);
    /* The corners come back in the order the sprite's UVs map onto: head-TL, head-TR, foot-BR,
     * foot-BL, and the triangle fan below preserves it. */
    const LightVertex tl = {o[0], o[1], depth, q->u0, q->v0, 1, 1, 1, 1};
    const LightVertex tr = {o[2], o[3], depth, q->u1, q->v0, 1, 1, 1, 1};
    const LightVertex br = {o[4], o[5], depth, q->u1, q->v1, 1, 1, 1, 1};
    const LightVertex bl = {o[6], o[7], depth, q->u0, q->v1, 1, 1, 1, 1};
    v[0] = tl;
    v[1] = tr;
    v[2] = br;
    v[3] = tl;
    v[4] = br;
    v[5] = bl;
}

/* The full-screen quad the light pass draws. */
static void light_emit_full(LightVertex *v, int w, int h)
{
    const LightVertex a = {0, 0, 0.5f, 0, 0, 1, 1, 1, 1};
    const LightVertex b = {(float)w, 0, 0.5f, 1, 0, 1, 1, 1, 1};
    const LightVertex c = {(float)w, (float)h, 0.5f, 1, 1, 1, 1, 1, 1};
    const LightVertex d = {0, (float)h, 0.5f, 0, 1, 1, 1, 1, 1};
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = a;
    v[4] = c;
    v[5] = d;
}

/* The lighting passes' vertex buffer. */
static int lvbuf_reserve(int bytes)
{
    if (lvbuf && lvbuf_cap >= bytes) return 1;
    if (lvbuf) {
        SDL_ReleaseGPUBuffer(DEV, lvbuf);
        lvbuf = NULL;
    }
    if (lvxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, lvxfer);
        lvxfer = NULL;
    }

    SDL_GPUBufferCreateInfo bi;
    SDL_zero(bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = (Uint32)bytes;
    lvbuf = SDL_CreateGPUBuffer(DEV, &bi);

    SDL_GPUTransferBufferCreateInfo ti;
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = (Uint32)bytes;
    lvxfer = SDL_CreateGPUTransferBuffer(DEV, &ti);

    if (!lvbuf || !lvxfer) {
        lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                       "engine: could not allocate a %d-byte light vertex buffer: %s\n", bytes, SDL_GetError());
        if (lvbuf) {
            SDL_ReleaseGPUBuffer(DEV, lvbuf);
            lvbuf = NULL;
        }
        if (lvxfer) {
            SDL_ReleaseGPUTransferBuffer(DEV, lvxfer);
            lvxfer = NULL;
        }
        lvbuf_cap = 0;
        return 0;
    }
    lvbuf_cap = bytes;
    return 1;
}

/* LF2_HD2D_SHOW=chars|shadow: present that mask instead of the lit frame, so a wrong-looking
 * picture can be told apart from an empty mask (the old hd2d chain's diagnostic, kept for the
 * same reason). There is no `albedo` any more: with the lighting engine-only, the unlit
 * picture IS the frame when the lighting is off, so the diagnostic would show nothing the
 * normal run does not. */
static int show_stage_name(void)
{
    static int looked, show;
    if (!looked) {
        looked = 1;
        const char *v = lf2_environment_get(LF2_ENV_HD2D_SHOW);
        show = v && strcmp(v, "chars") == 0 ? 1 : (v && strcmp(v, "shadow") == 0 ? 2 : 0);
    }
    return show;
}

SDL_GPUTexture *engine_lighting_run(const EngineQuad *q, int n, int w, int h)
{
    const int show = show_stage_name();
    const int want_chars = show == 1 || !show;
    const int want_shadow = show == 2 || !show;
    const int want_light = !show;

    if (!light_ok) return tex_color_bound;
    /* With the lighting off there is nothing to shade, and the masks' only reader is the light
     * pass -- building them would be work the frame does not use. The game's own ellipse was
     * recorded as a picture when the lighting was off (render_shadows_enabled), so the plain
     * picture is already the whole story. */
    if (want_light && !opt_lighting()) return tex_color_bound;

    int nc = 0, ns = 0;
    for (int i = 0; i < n; i++) {
        if (q[i].is_object) nc++;
        if (q[i].casts_shadow) ns++;
    }
    if (want_light && nc == 0 && ns == 0) return tex_color_bound;

    /* One buffer: chars quads, then shadow quads, then the full-screen light quad. */
    const int chars_v = nc * 6, shadow_v = ns * 6;
    if (!lvbuf_reserve((chars_v + shadow_v + 6) * (int)sizeof(LightVertex))) return tex_color_bound;

    float across = 0.0f, up = 0.0f;
    hd2d_shadow_project(&across, &up);

    void *map = SDL_MapGPUTransferBuffer(DEV, lvxfer, false);
    if (!map) return tex_color_bound;
    LightVertex *p = (LightVertex *)map;
    int cn = 0, sn = 0;
    for (int i = 0; i < n; i++) {
        const float depth = painter_depth(i, n);
        if (q[i].is_object) light_emit_char(p + (size_t)cn++ * 6, &q[i], depth);
        if (q[i].casts_shadow) light_emit_shadow(p + (size_t)(nc + sn++) * 6, &q[i], depth, across, up);
    }
    light_emit_full(p + (size_t)(nc + ns) * 6, w, h);
    SDL_UnmapGPUTransferBuffer(DEV, lvxfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(DEV);
    if (!cmd) return tex_color_bound;

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation from = {lvxfer, 0};
    SDL_GPUBufferRegion into = {lvbuf, 0, (Uint32)((chars_v + shadow_v + 6) * (int)sizeof(LightVertex))};
    SDL_UploadToGPUBuffer(copy, &from, &into, false);
    SDL_EndGPUCopyPass(copy);

    const float view[4] = {(float)w, (float)h, 0.0f, 0.0f};
    SDL_PushGPUVertexUniformData(cmd, 0, view, sizeof view);

    SDL_GPUBufferBinding vb = {lvbuf, 0};

    /* ---- the character mask ---- */
    if (want_chars) {
        SDL_GPUColorTargetInfo ci;
        SDL_zero(ci);
        ci.texture = tex_chars;
        ci.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 0.0f};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        /* LOAD the colour pass's completed depth. The mask quad carries the same painter
         * ordinal as its colour draw, so equal depth is visible; a later solid painter left a
         * smaller depth and rejects the hidden character fragment. */
        SDL_GPUDepthStencilTargetInfo visible;
        SDL_zero(visible);
        visible.texture = tex_depth_bound;
        visible.load_op = SDL_GPU_LOADOP_LOAD;
        visible.store_op = SDL_GPU_STOREOP_STORE;
        visible.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        visible.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &visible);
        if (pass) {
            SDL_BindGPUGraphicsPipeline(pass, p_chars);
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            int on = 0;
            for (int i = 0; i < n; i++) {
                if (!q[i].is_object) continue;
                SDL_GPUTexture *t = engine_texture_for(&q[i]);
                if (!t) {
                    on++;
                    continue;
                }
                const float geom[4] = {q[i].ground_gy, 1.0f / (float)h, 0.0f, 0.0f};
                SDL_GPUTextureSamplerBinding tsb = {t, smp_nearest};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                SDL_PushGPUFragmentUniformData(cmd, 0, geom, sizeof geom);
                SDL_DrawGPUPrimitives(pass, 6, 1, (Uint32)(on * 6), 0);
                stat_char_quads++;
                on++;
            }
            SDL_EndGPURenderPass(pass);
        }
    }

    /* ---- the cast-shadow mask ---- */
    if (want_shadow) {
        SDL_GPUColorTargetInfo ci;
        SDL_zero(ci);
        ci.texture = tex_shadow;
        ci.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 0.0f};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo receiver;
        SDL_zero(receiver);
        receiver.texture = tex_depth_bound;
        receiver.load_op = SDL_GPU_LOADOP_LOAD;
        receiver.store_op = SDL_GPU_STOREOP_STORE;
        receiver.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        receiver.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &receiver);
        if (pass) {
            SDL_BindGPUGraphicsPipeline(pass, p_shadow);
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            const float unused[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int on = 0;
            for (int i = 0; i < n; i++) {
                if (!q[i].casts_shadow) continue;
                SDL_GPUTexture *t = engine_texture_for(&q[i]);
                if (!t) {
                    on++;
                    continue;
                }
                SDL_GPUTextureSamplerBinding tsb = {t, smp_nearest};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                SDL_PushGPUFragmentUniformData(cmd, 0, unused, sizeof unused);
                SDL_DrawGPUPrimitives(pass, 6, 1, (Uint32)((nc + on) * 6), 0);
                stat_shadow_quads++;
                on++;
            }
            SDL_EndGPURenderPass(pass);
        }
    }

    /* ---- the light pass ---- */
    if (want_light) {
        SDL_GPUColorTargetInfo ci;
        SDL_zero(ci);
        ci.texture = tex_lit;
        ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ci, 1, NULL);
        if (pass) {
            SDL_BindGPUGraphicsPipeline(pass, p_light);
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUTextureSamplerBinding lb[3] = {
                {tex_color_bound, smp_nearest},
                {tex_chars, smp_linear},
                {tex_shadow, smp_linear},
            };
            SDL_BindGPUFragmentSamplers(pass, 0, lb, 3);
            float u[20];
            hd2d_light_uniforms(u, w, h);
            SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof u);
            SDL_DrawGPUPrimitives(pass, 6, 1, (Uint32)((nc + ns) * 6), 0);
            SDL_EndGPURenderPass(pass);
        }
        stat_light_frames++;
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    /* The mask the SHOW diagnostic asked for, not the one the light pass would read:
     * `want_chars` is true for both SHOW=chars and SHOW=shadow (a shadow needs the character
     * pass's mask to shade against), so the plain want-chars test handed SHOW=shadow the
     * CHARACTER mask and the diagnostic could never show a cast shadow (issue #72). */
    return want_light ? tex_lit : (show == 2 ? tex_shadow : tex_chars);
}

void engine_lighting_report(void)
{
    lf2_log_writef(LF2_LOG_INFO, "engine_lighting",
                   "engine: lighting %s -- %ld frame(s) lit, %ld character quad(s) into the "
                   "mask, %ld shadow quad(s)%s\n",
                   (light_ok && opt_lighting()) ? "ON" : "off", stat_light_frames, stat_char_quads, stat_shadow_quads,
                   light_ok ? "" : "  (NO lighting chain: frames are presented unlit)");
}

void engine_lighting_shutdown(void)
{
    if (!DEV) return;
    if (lvbuf) {
        SDL_ReleaseGPUBuffer(DEV, lvbuf);
        lvbuf = NULL;
    }
    if (lvxfer) {
        SDL_ReleaseGPUTransferBuffer(DEV, lvxfer);
        lvxfer = NULL;
    }
    lvbuf_cap = 0;
    if (p_chars) {
        SDL_ReleaseGPUGraphicsPipeline(DEV, p_chars);
        p_chars = NULL;
    }
    if (p_shadow) {
        SDL_ReleaseGPUGraphicsPipeline(DEV, p_shadow);
        p_shadow = NULL;
    }
    if (p_light) {
        SDL_ReleaseGPUGraphicsPipeline(DEV, p_light);
        p_light = NULL;
    }
    if (smp_linear) {
        SDL_ReleaseGPUSampler(DEV, smp_linear);
        smp_linear = NULL;
    }
}
