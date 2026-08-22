#include "render_snapshot.h"

SDL_Texture *render_target_make(SDL_Renderer *renderer, int width, int height, SDL_ScaleMode scale_mode)
{
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (texture) {
        SDL_SetTextureScaleMode(texture, scale_mode);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    }
    return texture;
}

void render_snapshot_init(RenderSnapshot *snapshot)
{
    snapshot->texture = NULL;
    frame_snapshot_init(&snapshot->life);
}

void render_snapshot_destroy(RenderSnapshot *snapshot)
{
    if (snapshot->texture) SDL_DestroyTexture(snapshot->texture);
    snapshot->texture = NULL;
    frame_snapshot_invalidate(&snapshot->life);
}

void render_snapshot_invalidate(RenderSnapshot *snapshot) { frame_snapshot_invalidate(&snapshot->life); }

static int neutral_target(SDL_Renderer *renderer, SDL_Texture *target)
{
    return SDL_SetRenderTarget(renderer, target) && SDL_SetRenderViewport(renderer, NULL) &&
           SDL_SetRenderClipRect(renderer, NULL) && SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

int render_snapshot_capture(RenderSnapshot *snapshot, SDL_Renderer *renderer, SDL_Texture *completed,
                            uint32_t source_pixels, int width, int height)
{
    if (snapshot->texture && (snapshot->life.width != width || snapshot->life.height != height))
        render_snapshot_destroy(snapshot);
    if (!snapshot->texture) snapshot->texture = render_target_make(renderer, width, height, SDL_SCALEMODE_NEAREST);

    int copied = snapshot->texture != NULL && neutral_target(renderer, snapshot->texture);
    if (copied) {
        SDL_SetTextureBlendMode(completed, SDL_BLENDMODE_NONE);
        copied = SDL_RenderTexture(renderer, completed, NULL, NULL);
    }
    SDL_SetRenderTarget(renderer, completed);
    frame_snapshot_captured(&snapshot->life, source_pixels, width, height, copied);
    return copied;
}

int render_snapshot_restore(RenderSnapshot *snapshot, SDL_Renderer *renderer, SDL_Texture *destination,
                            uint32_t source_pixels, int output_width, int output_height)
{
    if (!snapshot->texture || !frame_snapshot_can_freeze(&snapshot->life, source_pixels) ||
        !neutral_target(renderer, destination))
        return 0;

    SDL_FRect place;
    frame_snapshot_contain(snapshot->life.width, snapshot->life.height, output_width, output_height, &place.x, &place.y,
                           &place.w, &place.h);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (!SDL_RenderClear(renderer)) return 0;
    SDL_SetTextureBlendMode(snapshot->texture, SDL_BLENDMODE_NONE);
    if (!SDL_RenderTexture(renderer, snapshot->texture, NULL, &place)) return 0;
    frame_snapshot_presented_frozen(&snapshot->life);
    return 1;
}

long render_snapshot_frozen_frames(const RenderSnapshot *snapshot) { return snapshot->life.frozen_frames; }
