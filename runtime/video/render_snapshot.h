/* SDL resources for the immutable completed frame shown under RmlUi (issue #94). */
#ifndef LF2_RENDER_SNAPSHOT_H
#define LF2_RENDER_SNAPSHOT_H

#include "framesnapshot.h"

#include <SDL3/SDL.h>

typedef struct {
    SDL_Texture *texture;
    FrameSnapshot life;
} RenderSnapshot;

SDL_Texture *render_target_make(SDL_Renderer *renderer, int width, int height, SDL_ScaleMode scale_mode);
void render_snapshot_init(RenderSnapshot *snapshot);
void render_snapshot_destroy(RenderSnapshot *snapshot);
void render_snapshot_invalidate(RenderSnapshot *snapshot);
int render_snapshot_capture(RenderSnapshot *snapshot, SDL_Renderer *renderer, SDL_Texture *completed,
                            uint32_t source_pixels, int width, int height);
int render_snapshot_restore(RenderSnapshot *snapshot, SDL_Renderer *renderer, SDL_Texture *destination,
                            uint32_t source_pixels, int output_width, int output_height);
long render_snapshot_frozen_frames(const RenderSnapshot *snapshot);

#endif
