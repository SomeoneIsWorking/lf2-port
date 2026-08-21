#ifndef LF2_ENGINE_TEXTURES_H
#define LF2_ENGINE_TEXTURES_H

#include <stdint.h>

#include "engine.h"

struct SDL_GPUDevice;
struct SDL_GPUTexture;

void engine_textures_init(struct SDL_GPUDevice *device);
void engine_textures_begin_frame(void);
struct SDL_GPUTexture *engine_texture_for(const EngineQuad *quad);
void engine_textures_surface_dirty(uint32_t pixels);
void engine_textures_report(void);
void engine_textures_shutdown(void);

#endif
