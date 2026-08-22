/* Real-GPU visibility check for the engine's painter-depth and character-mask contract. */
#include "engine_visibility_probe.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"

enum {
    PROBE_W = 64,
    PROBE_H = 32,
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

static EngineQuad textured_quad(const uint32_t *pixels, int character)
{
    EngineQuad q;
    SDL_zero(q);
    q.x = 0.0f;
    q.y = 0.0f;
    q.w = (float)PROBE_W;
    q.h = (float)PROBE_H;
    q.u1 = 1.0f;
    q.v1 = 1.0f;
    q.r = q.g = q.b = q.a = 1.0f;
    q.host_argb = pixels;
    q.host_w = ART_W;
    q.host_h = ART_H;
    q.host_pitch = ART_W * 4;
    q.blend = 2; /* The shipping premultiplied host-art path. Both art colours use alpha 0/1. */
    q.is_object = character;
    /* Keep this synthetic character's cast shadow outside the target. The lit arm is checking
     * character-mask consumption, not the independent shadow-mask pass. */
    q.ground_cx = 1024.0f;
    q.ground_gy = 1024.0f;
    return q;
}

static int read_samples(SDL_Renderer *renderer, SDL_Texture *frame, uint32_t *left, uint32_t *right)
{
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_Texture *read_target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, PROBE_W, PROBE_H);
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

    const uint32_t *row =
        (const uint32_t *)((const uint8_t *)argb->pixels + (size_t)(PROBE_H / 2) * (size_t)argb->pitch);
    *left = row[PROBE_W / 4] & 0x00ffffffu;
    *right = row[(PROBE_W * 3) / 4] & 0x00ffffffu;
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

static int arm_passes(const char *arm, uint32_t left, uint32_t right)
{
    const uint32_t character = 0x00804020u;
    const uint32_t occluder = 0x002040c0u;
    if (strcmp(arm, "unlit") == 0) return near_rgb(left, occluder, 2) && near_rgb(right, character, 2);
    if (strcmp(arm, "chars") == 0) return !mask_on(left) && mask_on(right);
    if (strcmp(arm, "chars-reversed") == 0) return mask_on(left) && mask_on(right);
    if (strcmp(arm, "lit") == 0) {
        const int delta = abs(channel(right, 16) - channel(character, 16)) +
                          abs(channel(right, 8) - channel(character, 8)) +
                          abs(channel(right, 0) - channel(character, 0));
        return near_rgb(left, occluder, 2) && delta >= 20 && !near_rgb(right, 0, 2);
    }
    return 0;
}

void engine_visibility_probe_run(SDL_Renderer *renderer)
{
    const char *arm = getenv("LF2_VISIBILITY_PROBE");
    if (!arm || !*arm) return;
    if (strcmp(arm, "unlit") != 0 && strcmp(arm, "chars") != 0 && strcmp(arm, "chars-reversed") != 0 &&
        strcmp(arm, "lit") != 0) {
        fprintf(stderr, "visibility probe: FAIL unknown arm '%s'\n", arm);
        return;
    }

    make_art();
    EngineQuad quads[2] = {
        textured_quad(character_art, 1),
        textured_quad(occluder_art, 0),
    };
    if (strcmp(arm, "chars-reversed") == 0) {
        EngineQuad swap = quads[0];
        quads[0] = quads[1];
        quads[1] = swap;
    }

    SDL_Texture *frame = engine_draw(quads, 2, NULL, 0, PROBE_W, PROBE_H);
    uint32_t left = 0, right = 0;
    if (!frame || !read_samples(renderer, frame, &left, &right)) {
        fprintf(stderr, "visibility probe: FAIL arm=%s no rendered readback\n", arm);
        return;
    }
    fprintf(stderr, "visibility probe: %s arm=%s left=#%06x right=#%06x\n",
            arm_passes(arm, left, right) ? "PASS" : "FAIL", arm, left, right);
}
