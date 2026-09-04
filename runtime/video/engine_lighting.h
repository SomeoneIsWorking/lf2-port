/* The lighting chain of the engine: the character mask, the cast-shadow mask and the light
 * pass, as SDL_GPU passes over the finished picture (issues #37, #64, #69).
 *
 * WHY THIS IS ITS OWN FILE. The chain is one subsystem with its own pipelines, its own vertex
 * buffer, its own shaders and its own counters; it was living inside engine.c and pushing that
 * file over its line budget. What it does NOT own is the target set it draws into -- the
 * masks' textures are created by engine.c's targets_make and bound here -- and it does not
 * own the quad pipeline that draws the picture in the first place. The split is "what draws
 * the frame" (engine.c) versus "what re-lights the finished frame" (this file).
 *
 * The shaders were written for SDL_Render's varying convention (colour at location 0, uv at
 * location 1) and speak a different vertex format from the sprite pipeline's -- hd2d_quad.vert
 * explains. The three passes draw from ONE LightVertex buffer:
 *
 *   CHARS   the object quads again, through hd2d_character.frag, into a mask that says which
 *           pixels are a fighter and how high off the ground each is.
 *   SHADOW  the same objects' SHEARED silhouettes, through hd2d_shadow.frag, into the mask
 *           the light is taken away through.
 *   LIGHT   a full-screen pass through hd2d_light.frag that re-lights the picture from the
 *           one key light (hd2d.c), using the two masks.
 */
#ifndef LF2_ENGINE_LIGHTING_H
#define LF2_ENGINE_LIGHTING_H

#include <SDL3/SDL_gpu.h>

#include "engine.h"

/* Build the chain's shaders, pipelines and sampler. Returns 0 and says why through the logger when the
 * chain cannot run here -- frames are then presented unlit, which is a picture without
 * shading rather than a broken one. */
int engine_lighting_init(SDL_GPUDevice *dev, SDL_GPUShaderFormat formats, SDL_GPUTextureFormat depth_format);

/* The targets this chain reads and writes. Called again whenever the target set is rebuilt. */
void engine_lighting_bind_targets(SDL_GPUTexture *color, SDL_GPUTexture *depth, SDL_GPUTexture *chars,
                                  SDL_GPUTexture *shadow, SDL_GPUTexture *lit, SDL_GPUSampler *nearest);

/* True when even the chain's own mask sampler could not be built. The engine treats this as
 * fatal rather than as "unlit": the single-file arrangement refused there and must keep doing
 * so, or a half-working device would silently change how much of the port runs. */
int engine_lighting_sampler_missing(void);

/* Run the chain over `n` quads into the bound lit target (or the SHOW diagnostic's mask) and
 * return the GPU texture holding the frame to present -- tex_lit normally, tex_color when the
 * chain is off or has nothing to do. */
SDL_GPUTexture *engine_lighting_run(const EngineQuad *q, int n, int w, int h);

void engine_lighting_report(void);
void engine_lighting_shutdown(void);

#endif
