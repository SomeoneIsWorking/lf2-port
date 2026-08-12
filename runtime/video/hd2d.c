/* Isometric lighting and sprite-cast shadows -- see runtime/video/hd2d.h for the scope. */

#include "hd2d.h"
#include "stagelight.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders/gen/hd2d_gbuf_spv.h"
#include "shaders/gen/hd2d_light_spv.h"
#include "shaders/gen/hd2d_shadow_spv.h"

/* ---- the light rig ----
 *
 * The key light, as a direction in the stage's three axes: x across the screen, y up (LF2's
 * jump axis, claim C018), z toward the camera. Overhead, leaning slightly left and a little
 * in front of the fighters.
 *
 * It used to be (-0.55, 0.74, 0.38), which is a low SIDE light: it threw long shadows well
 * out to the right and lit one whole flank of a fighter while leaving the other on ambient.
 * A high key is the conventional one for this kind of art -- the shadow tucks in close under
 * the fighter and the shading reads as form rather than as a direction.
 *
 * This is the ONE place a light direction is written down. The shader shades with this vector
 * and hd2d_shadow_project projects it onto the ground for the cast shadows, so the two
 * cannot drift apart.
 */
/* DERIVED from the default angles rather than written out, because a vector default is a
 * second spelling of the same fact and the two had already drifted (see stagelight.h). Filled
 * lazily because mesh_init may ask for the light before hd2d has been initialised. */
static float LIGHT[3];
static int   light_ready;

/* The same direction as the two angles a player sets it with. Kept beside the vector rather
 * than derived back out of it, because going back is ambiguous at the poles and the menu
 * would jitter as it rounded. */
static float light_az = STAGELIGHT_AZ_DEFAULT, light_el = STAGELIGHT_EL_DEFAULT;

static void light_ensure(void)
{
    if (light_ready) return;
    light_ready = 1;
    stagelight_vector(light_az, light_el, LIGHT);
}

void hd2d_light_angles(float *az, float *el) { *az = light_az; *el = light_el; }
void hd2d_light_vector(float out[3])
{
    light_ensure();
    out[0] = LIGHT[0]; out[1] = LIGHT[1]; out[2] = LIGHT[2];
}

void hd2d_light_set_angles(float az, float el)
{
    /* The clamp, the wrap and the conversion are all stagelight.h's, INCLUDED rather than
     * copied -- ctest stagelight walks them offline, which is the only way anything about this
     * light could be asserted without booting the game and looking at a picture. */
    light_az = stagelight_wrap_azimuth(az);
    light_el = stagelight_clamp_elevation(el);
    stagelight_vector(light_az, light_el, LIGHT);
    light_ready = 1;
}

/* The shadow projection -- see hd2d.h. A point at height h above the ground casts to
 * h * (-Lx/Ly, -Lz/Ly) in the stage's own axes, and LF2's z projects straight down the
 * screen, so the second term is a screen-Y displacement UP the picture. Dividing by Ly is
 * what makes a low light throw a long shadow and a high one throw a short one -- the same
 * relationship the shading has, from the same vector. */
void hd2d_shadow_project(float *across, float *up)
{
    light_ensure();
    stagelight_shadow(LIGHT, across, up);
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

static Shader sh_chars, sh_light, sh_shadow;
static SDL_GPURenderState *st_chars, *st_light, *st_shadow;
static SDL_GPUSampler     *smp_linear;

/* The lighting state names its two extra textures, so it is rebuilt whenever they are --
 * which is every resize. */
static int chain_w, chain_h;

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
    sh_shadow = (Shader){ hd2d_shadow_spv, (int)sizeof hd2d_shadow_spv, 1, NULL };
    if (!shader_make(&sh_chars, "chars") || !shader_make(&sh_light, "light")
        || !shader_make(&sh_shadow, "shadow")) {
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
    ci.fragment_shader = sh_shadow.shader;
    st_shadow = SDL_CreateGPURenderState(R, &ci);
    if (!st_chars || !st_shadow) {
        init_why = "a render state failed";
        fprintf(stderr, "hd2d: render state: %s\n", SDL_GetError());
        return 0;
    }

    /* LF2_HD2D_LIGHT=<azimuth>,<elevation> in degrees. A DIAGNOSTIC: the light is the
     * player's, set from the pause menu's Options screen, and this exists so a test can put
     * it somewhere known and check that the shadows actually followed. "The shape responds to
     * the light" is not something a single screenshot can show. */
    {
        const char *v = getenv("LF2_HD2D_LIGHT");
        if (v) {
            float az = light_az, el = light_el;
            if (sscanf(v, "%f,%f", &az, &el) == 2) {
                hd2d_light_set_angles(az, el);
                fprintf(stderr, "hd2d: LF2_HD2D_LIGHT put the key at azimuth %.0f, elevation "
                                "%.0f\n", (double)light_az, (double)light_el);
            } else {
                fprintf(stderr, "hd2d: LF2_HD2D_LIGHT=%s is not <azimuth>,<elevation> -- the "
                                "light is UNCHANGED at %.0f,%.0f\n",
                        v, (double)light_az, (double)light_el);
            }
        }
    }

    init_ok = 1;
    init_why = "ok";
    fprintf(stderr, "hd2d: %s backend, SPIR-V shaders loaded -- isometric lighting and cast "
                    "shadows are running on the GPU\n", SDL_GetGPUDeviceDriver(DEV));
    return 1;
}

void hd2d_shutdown(void)
{
    SDL_GPURenderState **states[] = { &st_chars, &st_light, &st_shadow };
    for (unsigned i = 0; i < SDL_arraysize(states); i++)
        if (*states[i]) { SDL_DestroyGPURenderState(*states[i]); *states[i] = NULL; }
    Shader *shaders[] = { &sh_chars, &sh_light, &sh_shadow };
    for (unsigned i = 0; i < SDL_arraysize(shaders); i++)
        if (shaders[i]->shader && DEV) {
            SDL_ReleaseGPUShader(DEV, shaders[i]->shader);
            shaders[i]->shader = NULL;
        }
    if (smp_linear && DEV) { SDL_ReleaseGPUSampler(DEV, smp_linear); smp_linear = NULL; }
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
static int chain_build(SDL_Texture *chars, SDL_Texture *shadow, int w, int h)
{
    if (st_light) { SDL_DestroyGPURenderState(st_light); st_light = NULL; }

    /* light: the albedo is sampler 0 (the texture being drawn), then the character buffer and
     * the cast-shadow mask -- both at FULL resolution, both sampled 1:1.
     *
     * The mask used to be downsampled to half and Gaussian-blurred on the way. That is what a
     * soft shadow wants and it is not what this game wants: a 32-pixel sprite's silhouette,
     * halved and then blurred, is a shapeless dark smear with none of the fighter left in it.
     * The shadow is crisp now, which also means the blur shader and the two scratch targets
     * that existed only to feed it are gone. */
    SDL_GPUTextureSamplerBinding lb[2] = {
        { gpu_of(chars),  smp_linear },
        { gpu_of(shadow), smp_linear },
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

/* ---- looking at one stage of the chain ----
 *
 * LF2_HD2D_SHOW=albedo|chars|shadow presents that buffer instead of the lit frame. Every
 * failure in here looks the same from outside -- a picture that is slightly wrong -- and
 * "the fighters look flat" is a bevel that is too tight, a character buffer with nothing in
 * it, or a shadow mask that was never drawn. Looking at the buffer says which.
 *
 * It shows each buffer AT THE POINT THAT BUFFER IS FINAL. An earlier version looked them all
 * up at the end of the chain, by which time a scratch target that had held the shadow mask
 * had been reused -- so `SHOW=shadow` displayed a blurred copy of the scene and read as "the
 * shadow mask contains the whole picture". A debug view that shows the wrong buffer is worse
 * than none: it answers confidently. There is no scratch left to be reused now, but the
 * ordering is kept, because the next thing added here would bring one back.
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
              int w, int h, float floor_row, int have_floor)
{
    if (!hd2d_ready() || !albedo || !chars || !shadow || !out) return 0;
    if (chain_w != w || chain_h != h) {
        if (!chain_build(chars, shadow, w, h)) { chain_w = chain_h = 0; return 0; }
    }

    if (show_is("albedo")) return show_stage(albedo, out);
    if (show_is("chars"))  return show_stage(chars, out);
    if (show_is("shadow")) return show_stage(shadow, out);

    struct { Vec4 sun_dir, sun_color, sky, bounce, params, floor; } u;
    /* The lazy fill, and this is the site that made it necessary to be careful: LIGHT is no
     * longer a literal initialiser, so a pass that read it before anything had filled it would
     * get (0,0,0) and light every fighter from nowhere. Every reader goes through this. */
    light_ensure();
    u.sun_dir   = (Vec4){ LIGHT[0], LIGHT[1], LIGHT[2], knob("LF2_HD2D_KEY", 1.48f) };
    /* A warm key against a cool sky is what puts a temperature difference between the lit
     * side and the shaded side of a fighter, which is what makes flat art read as having a
     * form rather than just a brightness. */
    u.sun_color = (Vec4){ 1.10f, 1.02f, 0.90f, knob("LF2_HD2D_AMBIENT", 0.66f) };
    u.sky       = (Vec4){ 0.62f, 0.68f, 0.80f, knob("LF2_HD2D_BEVEL", 0.90f) };
    u.bounce    = (Vec4){ 0.55f, 0.52f, 0.50f, knob("LF2_HD2D_SHADOW", 0.55f) };
    u.params    = (Vec4){ 1.0f / (float)w, 1.0f / (float)h,
                          knob("LF2_HD2D_BEVEL_PX", 5.0f),
                          knob("LF2_HD2D_HEIGHT_GAIN", 0.9f) };
    /* The feather is a few rows: the floor meets the wall at a line the art already draws,
     * and a hard switch there would put a second, straighter line beside it. */
    const float feather = knob("LF2_HD2D_FLOOR_FEATHER", 6.0f);
    u.floor = (Vec4){ floor_row, 1.0f / (feather > 0.5f ? feather : 0.5f),
                      have_floor ? 1.0f : 0.0f, 0.0f };
    SDL_SetGPURenderStateFragmentUniforms(st_light, 0, &u, sizeof u);
    pass(out, albedo, st_light);

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
