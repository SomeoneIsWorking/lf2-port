/* Isometric lighting and sprite-cast shadows -- see runtime/hd2d.h for the scope. */

#include "hd2d.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders/gen/hd2d_gbuf_spv.h"
#include "shaders/gen/hd2d_light_spv.h"
#include "shaders/gen/hd2d_blur_spv.h"
#include "shaders/gen/hd2d_shadow_spv.h"

/* ---- the light rig ----
 *
 * The key light, as a direction in the stage's three axes: x across the screen, y up (LF2's
 * jump axis, claim C018), z toward the camera. Upper-left and slightly in front of the
 * fighters, which puts a shadow down and to the right where a player expects one.
 *
 * These are the ONLY light constants in the port. The shader shades with this vector and
 * hd2d_shadow_lean projects it onto the ground for the cast shadows, so there is no second
 * place where a shadow direction is written down and no way for the two to drift apart.
 */
static const float LIGHT[3] = { -0.55f, 0.74f, 0.38f };

/* A shadow lies on the ground, so its shear is the light direction projected onto that
 * ground: the top of the laid-down sprite moves AWAY from the light by (-x/y) per unit of
 * height. Dividing by y is what makes a low sun throw a long shadow and a high one throw a
 * short one, which is the same relationship the shading has. */
float hd2d_shadow_lean(void)
{
    const float y = LIGHT[1] < 0.05f ? 0.05f : LIGHT[1];
    return -LIGHT[0] / y;
}

/* ---- state ---- */

typedef struct {
    const unsigned char *spv;
    int          spv_len;
    int          samplers;
    SDL_GPUShader *shader;
} Shader;

static SDL_Renderer  *R;
static SDL_GPUDevice *DEV;

static Shader sh_chars, sh_light, sh_blur, sh_shadow;
static SDL_GPURenderState *st_chars, *st_light, *st_blur, *st_shadow;
static SDL_GPUSampler     *smp_linear;

/* The shadow mask is softened at half resolution: cheaper, and softer than blurring it at
 * full size. Rebuilt, along with the render state that names one, on every resize. */
static SDL_Texture *t_half0, *t_half1;
static int          chain_w, chain_h;

static long stat_runs, stat_char_quads;
static int  init_done, init_ok;
static const char *init_why = "not attempted";

int hd2d_wanted(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("LF2_HD2D");
        on = !(v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0));
    }
    return on;
}

int hd2d_ready(void) { return init_ok && hd2d_wanted(); }

static int shader_make(Shader *s, const char *name)
{
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.code = s->spv;
    info.code_size = (size_t)s->spv_len;
    info.entrypoint = "main";
    info.num_samplers = (Uint32)s->samplers;
    info.num_uniform_buffers = 1;
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    s->shader = SDL_CreateGPUShader(DEV, &info);
    if (!s->shader) {
        fprintf(stderr, "hd2d: shader %s could not be created: %s\n", name, SDL_GetError());
        return 0;
    }
    return 1;
}

int hd2d_init(SDL_Renderer *r)
{
    if (init_done) return init_ok;
    init_done = 1;
    R = r;
    if (!hd2d_wanted()) { init_why = "LF2_HD2D=off"; return 0; }

    DEV = SDL_GetGPURendererDevice(r);
    if (!DEV) {
        /* Not a warning to be swallowed: without a GPU device there are no shaders, and the
         * player gets the plain composition. Say which renderer is up, because the usual
         * cause is that SDL chose one that is not the GPU renderer at all. */
        init_why = "the renderer has no GPU device";
        fprintf(stderr, "hd2d: the '%s' renderer has no GPU device, so there is no shader "
                        "path and the lighting CANNOT run -- the picture will be the plain "
                        "composition. The GPU renderer is the one this needs.\n",
                SDL_GetRendererName(r));
        return 0;
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(DEV);
    if (!(formats & SDL_GPU_SHADERFORMAT_SPIRV)) {
        /* This is the honest state of the port on a Metal or D3D12 backend: the shaders are
         * shipped as SPIR-V only. Saying so beats shipping a second approximation. */
        init_why = "the GPU backend does not take SPIR-V";
        fprintf(stderr, "hd2d: the %s backend accepts shader formats 0x%x, which does not "
                        "include SPIR-V (0x%x) -- this port ships SPIR-V only, so the "
                        "lighting cannot run here and the picture is the plain composition.\n",
                SDL_GetGPUDeviceDriver(DEV), (unsigned)formats,
                (unsigned)SDL_GPU_SHADERFORMAT_SPIRV);
        return 0;
    }

    sh_chars = (Shader){ hd2d_gbuf_spv,  (int)sizeof hd2d_gbuf_spv,  1, NULL };
    sh_light = (Shader){ hd2d_light_spv, (int)sizeof hd2d_light_spv, 3, NULL };
    sh_blur  = (Shader){ hd2d_blur_spv,  (int)sizeof hd2d_blur_spv,  1, NULL };
    sh_shadow = (Shader){ hd2d_shadow_spv, (int)sizeof hd2d_shadow_spv, 1, NULL };
    if (!shader_make(&sh_chars, "chars") || !shader_make(&sh_light, "light")
        || !shader_make(&sh_blur, "blur") || !shader_make(&sh_shadow, "shadow")) {
        init_why = "a shader failed to compile";
        return 0;
    }

    SDL_GPUSamplerCreateInfo si;
    SDL_zero(si);
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    smp_linear = SDL_CreateGPUSampler(DEV, &si);
    if (!smp_linear) {
        init_why = "no sampler";
        fprintf(stderr, "hd2d: could not create a sampler: %s\n", SDL_GetError());
        return 0;
    }

    /* Neither of these binds a texture by name -- the one being drawn is sampler 0 -- so
     * they survive a resize and are made once. */
    SDL_GPURenderStateCreateInfo ci;
    SDL_zero(ci);
    ci.fragment_shader = sh_chars.shader;
    st_chars = SDL_CreateGPURenderState(R, &ci);
    SDL_zero(ci);
    ci.fragment_shader = sh_blur.shader;
    st_blur = SDL_CreateGPURenderState(R, &ci);
    SDL_zero(ci);
    ci.fragment_shader = sh_shadow.shader;
    st_shadow = SDL_CreateGPURenderState(R, &ci);
    if (!st_chars || !st_blur || !st_shadow) {
        init_why = "a render state failed";
        fprintf(stderr, "hd2d: render state: %s\n", SDL_GetError());
        return 0;
    }

    init_ok = 1;
    init_why = "ok";
    fprintf(stderr, "hd2d: %s backend, SPIR-V shaders loaded -- isometric lighting and cast "
                    "shadows are running on the GPU\n", SDL_GetGPUDeviceDriver(DEV));
    return 1;
}

void hd2d_shutdown(void)
{
    SDL_GPURenderState **states[] = { &st_chars, &st_light, &st_blur, &st_shadow };
    for (unsigned i = 0; i < SDL_arraysize(states); i++)
        if (*states[i]) { SDL_DestroyGPURenderState(*states[i]); *states[i] = NULL; }
    Shader *shaders[] = { &sh_chars, &sh_light, &sh_blur, &sh_shadow };
    for (unsigned i = 0; i < SDL_arraysize(shaders); i++)
        if (shaders[i]->shader && DEV) {
            SDL_ReleaseGPUShader(DEV, shaders[i]->shader);
            shaders[i]->shader = NULL;
        }
    if (smp_linear && DEV) { SDL_ReleaseGPUSampler(DEV, smp_linear); smp_linear = NULL; }
    if (t_half0) { SDL_DestroyTexture(t_half0); t_half0 = NULL; }
    if (t_half1) { SDL_DestroyTexture(t_half1); t_half1 = NULL; }
    chain_w = chain_h = 0;
    init_done = init_ok = 0;
    R = NULL; DEV = NULL;
}

/* ---- the character buffer ---- */

typedef struct { float x, y, z, w; } Vec4;

static float chars_inv_h = 1.0f / 550.0f;

int hd2d_chars_begin(float inv_view_height)
{
    if (!hd2d_ready() || !st_chars) return 0;
    chars_inv_h = inv_view_height;
    return SDL_SetGPURenderState(R, st_chars);
}

void hd2d_chars_quad(float ground_y)
{
    if (!st_chars) return;
    const Vec4 u = { ground_y, chars_inv_h, 0.0f, 0.0f };
    /* SDL flushes the queued draws when this changes, so each quad gets its own uniforms
     * rather than the last one set before the batch was submitted. */
    SDL_SetGPURenderStateFragmentUniforms(st_chars, 0, &u, sizeof u);
    stat_char_quads++;
}

void hd2d_chars_end(void) { if (R) SDL_SetGPURenderState(R, NULL); }

/* The shadow mask pass. Same shape as the character buffer: render.c draws the objects'
 * laid-down quads with this active, and the shader writes coverage rather than colour. */
int hd2d_shadow_begin(void)
{
    if (!hd2d_ready() || !st_shadow) return 0;
    const Vec4 u = { 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_SetGPURenderStateFragmentUniforms(st_shadow, 0, &u, sizeof u);
    return SDL_SetGPURenderState(R, st_shadow);
}

void hd2d_shadow_end(void) { if (R) SDL_SetGPURenderState(R, NULL); }

/* ---- the light ---- */

static SDL_GPUTexture *gpu_of(SDL_Texture *t)
{
    return (SDL_GPUTexture *)SDL_GetPointerProperty(SDL_GetTextureProperties(t),
                                                    SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, NULL);
}

/* The lighting state's sampler bindings name two textures, so it has to be rebuilt whenever
 * either is -- which is every resize. Keeping it alive across one would leave the shader
 * reading a destroyed shadow buffer: a use-after-free that shows up as garbage light rather
 * than as a crash. */
static int chain_build(SDL_Texture *chars, int w, int h)
{
    if (t_half0) { SDL_DestroyTexture(t_half0); t_half0 = NULL; }
    if (t_half1) { SDL_DestroyTexture(t_half1); t_half1 = NULL; }
    if (st_light) { SDL_DestroyGPURenderState(st_light); st_light = NULL; }

    const int hw = w / 2 > 1 ? w / 2 : 1, hh = h / 2 > 1 ? h / 2 : 1;
    for (int i = 0; i < 2; i++) {
        SDL_Texture **slot = i ? &t_half1 : &t_half0;
        *slot = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_TARGET, hw, hh);
        if (!*slot) {
            fprintf(stderr, "hd2d: could not create the %dx%d shadow buffers: %s\n",
                    hw, hh, SDL_GetError());
            return 0;
        }
        SDL_SetTextureScaleMode(*slot, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(*slot, SDL_BLENDMODE_NONE);
    }

    /* light: the albedo is sampler 0 (the texture being drawn), then the character buffer
     * and the softened shadow mask, which lands in t_half1. */
    SDL_GPUTextureSamplerBinding lb[2] = {
        { gpu_of(chars),   smp_linear },
        { gpu_of(t_half1), smp_linear },
    };
    SDL_GPURenderStateCreateInfo ci;
    SDL_zero(ci);
    ci.fragment_shader = sh_light.shader;
    ci.num_sampler_bindings = 2;
    ci.sampler_bindings = lb;
    st_light = SDL_CreateGPURenderState(R, &ci);
    if (!st_light) {
        fprintf(stderr, "hd2d: could not create the lighting render state: %s\n",
                SDL_GetError());
        return 0;
    }
    chain_w = w; chain_h = h;
    return 1;
}

/* One full-target pass: `src` becomes sampler 0, `state` supplies the rest. */
static void pass(SDL_Texture *dst, SDL_Texture *src, SDL_GPURenderState *state)
{
    SDL_SetRenderTarget(R, dst);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    if (state) SDL_SetGPURenderState(R, state);
    SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(R, src, NULL, NULL);
    if (state) SDL_SetGPURenderState(R, NULL);
}

/* A separable Gaussian, in the source's texels. */
static void blur(SDL_Texture *dst, SDL_Texture *tmp, SDL_Texture *src, float radius)
{
    float w = 1.0f, h = 1.0f;
    SDL_GetTextureSize(src, &w, &h);
    Vec4 u = { radius / w, 0.0f, 0.0f, 0.0f };
    SDL_SetGPURenderStateFragmentUniforms(st_blur, 0, &u, sizeof u);
    pass(tmp, src, st_blur);
    SDL_GetTextureSize(tmp, &w, &h);
    u = (Vec4){ 0.0f, radius / h, 0.0f, 0.0f };
    SDL_SetGPURenderStateFragmentUniforms(st_blur, 0, &u, sizeof u);
    pass(dst, tmp, st_blur);
}

/* ---- looking at one stage of the chain ----
 *
 * LF2_HD2D_SHOW=albedo|chars|shadow presents that buffer instead of the lit frame. Every
 * failure in here looks the same from outside -- a picture that is slightly wrong -- and
 * "the fighters look flat" is a bevel that is too tight, a character buffer with nothing in
 * it, or a shadow mask that was never drawn. Looking at the buffer says which.
 *
 * IT SHOWS EACH BUFFER AT THE MOMENT THAT BUFFER IS FINAL. The first version looked them all
 * up at the END of the chain, by which time the half-resolution scratch that had held the
 * shadow mask had been reused -- so `SHOW=shadow` displayed a blurred copy of the scene and
 * read as "the shadow mask contains the whole picture". A debug view that shows the wrong
 * buffer is worse than none: it answers confidently.
 */
static int show_is(const char *name)
{
    static const char *want;
    static int looked;
    if (!looked) { looked = 1; want = getenv("LF2_HD2D_SHOW"); }
    return want && strcmp(want, name) == 0;
}

static int show_named_something;

static int show_stage(SDL_Texture *src, SDL_Texture *out)
{
    show_named_something = 1;
    pass(out, src, NULL);
    SDL_SetRenderTarget(R, NULL);
    stat_runs++;
    return 1;
}

/* The look, as numbers. They are chosen so that a FLAT, unshadowed, camera-facing pixel comes
 * out at very close to the colour the game drew -- ambient*hemisphere + key*n.L with the
 * normal facing the camera sums to about 1.0. That is deliberate, and it is the discipline of
 * this pass: the light must not be a brightness or a tint applied to the game. What is
 * visible is the DIFFERENCE from flat -- the bevel round a fighter's silhouette, the sky
 * catching them when they jump, and the shadow they throw.
 *
 * LF2_HD2D_* are for sweeping these while tuning, not configuration. */
static float knob(const char *name, float dflt)
{
    const char *v = getenv(name);
    return v ? (float)atof(v) : dflt;
}

int hd2d_post(SDL_Texture *albedo, SDL_Texture *chars, SDL_Texture *shadow, SDL_Texture *out,
              int w, int h)
{
    if (!hd2d_ready() || !albedo || !chars || !shadow || !out) return 0;
    if (chain_w != w || chain_h != h) {
        if (!chain_build(chars, w, h)) { chain_w = chain_h = 0; return 0; }
    }

    if (show_is("albedo")) return show_stage(albedo, out);
    if (show_is("chars"))  return show_stage(chars, out);

    /* 1. The cast-shadow mask, softened. It is drawn at full resolution from the sprites'
     *    own silhouettes; a hard edge off a 32-pixel sprite magnified to 1080p reads as a
     *    decal rather than as a shadow. */
    pass(t_half1, shadow, NULL);
    blur(t_half1, t_half0, t_half1, knob("LF2_HD2D_SHADOW_BLUR", 1.3f));
    if (show_is("shadow")) return show_stage(t_half1, out);

    /* 2. The light. */
    {
        struct { Vec4 sun_dir, sun_color, sky, bounce, params; } u;
        u.sun_dir   = (Vec4){ LIGHT[0], LIGHT[1], LIGHT[2], knob("LF2_HD2D_KEY", 1.06f) };
        /* A warm key against a cool sky is what puts a temperature difference between the
         * lit side and the shaded side of a fighter, which is what makes flat art read as
         * having a form. */
        u.sun_color = (Vec4){ 1.10f, 1.02f, 0.90f, knob("LF2_HD2D_AMBIENT", 0.85f) };
        u.sky       = (Vec4){ 0.62f, 0.68f, 0.80f, knob("LF2_HD2D_BEVEL", 0.55f) };
        u.bounce    = (Vec4){ 0.55f, 0.52f, 0.50f, knob("LF2_HD2D_SHADOW", 0.55f) };
        u.params    = (Vec4){ 1.0f / (float)w, 1.0f / (float)h,
                              knob("LF2_HD2D_BEVEL_PX", 4.0f),
                              knob("LF2_HD2D_HEIGHT_GAIN", 0.9f) };
        SDL_SetGPURenderStateFragmentUniforms(st_light, 0, &u, sizeof u);
        pass(out, albedo, st_light);
    }

    SDL_SetRenderTarget(R, NULL);
    stat_runs++;
    return 1;
}

void hd2d_report(void)
{
    if (!getenv("LF2_RENDER_DEBUG")) return;
    fprintf(stderr, "hd2d: wanted=%s shaders=%s (%s); lit %ld frame(s) from %ld character "
                    "quads\n",
            hd2d_wanted() ? "yes" : "no", init_ok ? "loaded" : "NOT LOADED", init_why,
            stat_runs, stat_char_quads);
    if (hd2d_wanted() && !init_ok)
        fprintf(stderr, "hd2d: the pass never ran -- %s -- so the picture is the plain "
                        "composition and every number above describes nothing\n", init_why);
    if (getenv("LF2_HD2D_SHOW") && !show_named_something)
        fprintf(stderr, "hd2d: LF2_HD2D_SHOW=%s matched NO stage of the chain, so what was "
                        "shown is the lit frame -- try albedo, chars or shadow\n",
                getenv("LF2_HD2D_SHOW"));
    if (init_ok && stat_runs && !stat_char_quads)
        fprintf(stderr, "hd2d: the pass ran on %ld frame(s) but NOTHING was written to the "
                        "character buffer, so no fighter was lit and the only visible effect "
                        "is the cast shadows\n", stat_runs);
}
