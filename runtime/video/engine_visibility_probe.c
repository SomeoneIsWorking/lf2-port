/* Real-GPU visibility check for the engine's painter-depth and character-mask contract. */
#include "engine_visibility_probe.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"

enum {
    PROBE_W = 128,
    PROBE_H = 64,
    ART_W = 8,
    ART_H = 4,
};

static uint32_t character_art[ART_W * ART_H];
static uint32_t occluder_art[ART_W * ART_H];

static void make_art(void)
{
    static int made;
    if (made) return;
    made = 1;
    for (int y = 0; y < ART_H; y++) {
        for (int x = 0; x < ART_W; x++) {
            character_art[y * ART_W + x] = 0xff804020u;
            occluder_art[y * ART_W + x] = x < ART_W / 2 ? 0xff2040c0u : 0x00000000u;
        }
    }
}

static EngineQuad textured_quad(const uint32_t *pixels, float x, float y, float w, float h,
                                int character, int caster, float ground_y)
{
    EngineQuad q;
    SDL_zero(q);
    q.x = x;
    q.y = y;
    q.w = w;
    q.h = h;
    q.u1 = 1.0f;
    q.v1 = 1.0f;
    q.r = q.g = q.b = q.a = 1.0f;
    q.host_argb = pixels;
    q.host_w = ART_W;
    q.host_h = ART_H;
    q.host_pitch = ART_W * 4;
    q.blend = 2; /* The shipping premultiplied host-art path. Both art colours use alpha 0/1. */
    q.is_object = character;
    q.casts_shadow = caster;
    q.ground_gy = ground_y;
    return q;
}

static int read_samples(SDL_Renderer *renderer, SDL_Texture *frame, int width, int height,
                        const int *xy, int count, uint32_t *rgb)
{
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_Texture *read_target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!read_target) {
        fprintf(stderr, "visibility probe: could not create readback target: %s\n", SDL_GetError());
        return 0;
    }

    int ok = SDL_SetRenderTarget(renderer, read_target);
    if (ok) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ok = SDL_RenderClear(renderer);
    }
    if (ok) {
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
        ok = SDL_RenderTexture(renderer, frame, NULL, NULL);
    }
    SDL_Surface *surface = ok ? SDL_RenderReadPixels(renderer, NULL) : NULL;
    SDL_SetRenderTarget(renderer, previous);

    if (!surface) {
        fprintf(stderr, "visibility probe: output readback failed: %s\n", SDL_GetError());
        SDL_DestroyTexture(read_target);
        return 0;
    }
    SDL_Surface *argb =
        surface->format == SDL_PIXELFORMAT_ARGB8888 ? surface : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    if (!argb) {
        fprintf(stderr, "visibility probe: output conversion failed: %s\n", SDL_GetError());
        SDL_DestroySurface(surface);
        SDL_DestroyTexture(read_target);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)argb->pixels +
                                                 (size_t)xy[i * 2 + 1] * (size_t)argb->pitch);
        rgb[i] = row[xy[i * 2]] & 0x00ffffffu;
    }
    if (argb != surface) SDL_DestroySurface(argb);
    SDL_DestroySurface(surface);
    SDL_DestroyTexture(read_target);
    return 1;
}

static int channel(uint32_t rgb, int shift) { return (int)((rgb >> shift) & 0xffu); }

static int near_rgb(uint32_t actual, uint32_t expected, int tolerance)
{
    for (int shift = 0; shift <= 16; shift += 8)
        if (abs(channel(actual, shift) - channel(expected, shift)) > tolerance) return 0;
    return 1;
}

static int mask_on(uint32_t rgb) { return channel(rgb, 16) >= 240; }

static int arm_passes(const char *arm, const uint32_t *rgb)
{
    const uint32_t character = 0x00804020u;
    const uint32_t occluder = 0x002040c0u;
    if (strcmp(arm, "unlit") == 0) return near_rgb(rgb[0], occluder, 2) && near_rgb(rgb[1], character, 2);
    if (strcmp(arm, "chars") == 0) return !mask_on(rgb[0]) && mask_on(rgb[1]);
    if (strcmp(arm, "chars-reversed") == 0) return mask_on(rgb[0]) && mask_on(rgb[1]);
    if (strcmp(arm, "lit") == 0) {
        const int delta = abs(channel(rgb[1], 16) - channel(character, 16)) +
                          abs(channel(rgb[1], 8) - channel(character, 8)) +
                          abs(channel(rgb[1], 0) - channel(character, 0));
        return near_rgb(rgb[0], occluder, 2) && delta >= 20 && !near_rgb(rgb[1], 0, 2);
    }
    if (strcmp(arm, "shadow-carried") == 0) return mask_on(rgb[0]) && mask_on(rgb[1]) && !mask_on(rgb[2]);
    if (strcmp(arm, "shadow-fighter-only") == 0) return mask_on(rgb[0]) && !mask_on(rgb[1]) && !mask_on(rgb[2]);
    if (strcmp(arm, "shadow-occluded") == 0)
        return !mask_on(rgb[0]) && mask_on(rgb[1]) && mask_on(rgb[2]) && !mask_on(rgb[3]);
    if (strcmp(arm, "shadow-occluded-reversed") == 0)
        return mask_on(rgb[0]) && mask_on(rgb[1]) && mask_on(rgb[2]) && !mask_on(rgb[3]);
    if (strcmp(arm, "shadow-self-lequal") == 0)
        return !mask_on(rgb[0]) && mask_on(rgb[1]) && mask_on(rgb[2]) && mask_on(rgb[3]);
    return 0;
}

void engine_visibility_probe_run(SDL_Renderer *renderer)
{
    const char *arm = getenv("LF2_VISIBILITY_PROBE");
    if (!arm || !*arm) return;
    const int shadow_arm = strncmp(arm, "shadow-", 7) == 0;
    if (strcmp(arm, "unlit") != 0 && strcmp(arm, "chars") != 0 && strcmp(arm, "chars-reversed") != 0 &&
        strcmp(arm, "lit") != 0 && strcmp(arm, "shadow-carried") != 0 && strcmp(arm, "shadow-fighter-only") != 0 &&
        strcmp(arm, "shadow-occluded") != 0 && strcmp(arm, "shadow-occluded-reversed") != 0 &&
        strcmp(arm, "shadow-self-lequal") != 0) {
        fprintf(stderr, "visibility probe: FAIL unknown arm '%s'\n", arm);
        return;
    }

    make_art();
    EngineQuad quads[3];
    int quad_count = 2;
    int xy[8] = {0};
    if (strcmp(arm, "shadow-carried") == 0 || strcmp(arm, "shadow-fighter-only") == 0) {
        quads[0] = textured_quad(character_art, 28, 28, 8, 20, 0, 1, 48);
        quads[1] = textured_quad(character_art, 16, 4, 32, 12, 0, strcmp(arm, "shadow-carried") == 0, 48);
        const int sample[6] = {46, 40, 65, 29, 110, 50};
        memcpy(xy, sample, sizeof sample);
    } else if (strcmp(arm, "shadow-occluded") == 0 || strcmp(arm, "shadow-occluded-reversed") == 0 ||
               strcmp(arm, "shadow-self-lequal") == 0) {
        /* An opaque ground receiver is painted first, then the caster, then the half-opaque
         * foreground object. The four samples distinguish later occlusion, transparent
         * reception, earlier opaque reception, and equal-depth self-shadowing. */
        quads[0] = textured_quad(character_art, 0, 0, PROBE_W, PROBE_H, 0, 0, 0);
        quads[1] = textured_quad(character_art, 12, 28, 16, 20, 0, 1, 48);
        quads[2] = textured_quad(occluder_art, 30, 36, 12, 12, 0, 0, 0);
        quad_count = 3;
        const int sample[8] = {33, 40, 39, 40, 29, 40, 25, 44};
        memcpy(xy, sample, sizeof sample);
    } else {
        quads[0] = textured_quad(character_art, 0, 0, PROBE_W, PROBE_H, 1, 0, 0);
        quads[1] = textured_quad(occluder_art, 0, 0, PROBE_W, PROBE_H, 0, 0, 0);
        const int sample[6] = {PROBE_W / 4, PROBE_H / 2, PROBE_W * 3 / 4, PROBE_H / 2, 0, 0};
        memcpy(xy, sample, sizeof sample);
    }
    if (strcmp(arm, "chars-reversed") == 0) {
        EngineQuad swap = quads[0];
        quads[0] = quads[1];
        quads[1] = swap;
    } else if (strcmp(arm, "shadow-occluded-reversed") == 0) {
        EngineQuad swap = quads[1];
        quads[1] = quads[2];
        quads[2] = swap;
    }

    SDL_Texture *frame = engine_draw(quads, quad_count, NULL, 0, PROBE_W, PROBE_H);
    uint32_t rgb[4] = {0, 0, 0, 0};
    const int count =
        strncmp(arm, "shadow-occluded", 15) == 0 || strcmp(arm, "shadow-self-lequal") == 0 ? 4 : (shadow_arm ? 3 : 2);
    if (!frame || !read_samples(renderer, frame, PROBE_W, PROBE_H, xy, count, rgb)) {
        fprintf(stderr, "visibility probe: FAIL arm=%s no rendered readback\n", arm);
        return;
    }
    fprintf(stderr, "visibility probe: %s arm=%s left=#%06x right=#%06x third=#%06x fourth=#%06x\n",
            arm_passes(arm, rgb) ? "PASS" : "FAIL", arm, rgb[0], rgb[1], rgb[2], rgb[3]);
}
