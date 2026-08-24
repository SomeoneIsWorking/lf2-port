#ifndef LF2_GPU_DEPTH_FORMAT_H
#define LF2_GPU_DEPTH_FORMAT_H

#include <SDL3/SDL_gpu.h>
#include <stddef.h>

/* D32 is preferred, not required. SDL GPU exposes support per backend and Metal is allowed to
 * offer a different depth-target format. D16 still has eight times the precision needed for
 * render.c's bounded 8,192-entry painter list, so refusing the engine there is not correctness. */
static inline SDL_GPUTextureFormat gpu_depth_format_select(SDL_GPUDevice *device)
{
    static const SDL_GPUTextureFormat candidates[] = {
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++)
        if (SDL_GPUTextureSupportsFormat(device, candidates[i], SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            return candidates[i];
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static inline const char *gpu_depth_format_name(SDL_GPUTextureFormat format)
{
    if (format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT) return "D32_FLOAT";
    if (format == SDL_GPU_TEXTUREFORMAT_D24_UNORM) return "D24_UNORM";
    if (format == SDL_GPU_TEXTUREFORMAT_D16_UNORM) return "D16_UNORM";
    return "unsupported";
}

#endif
