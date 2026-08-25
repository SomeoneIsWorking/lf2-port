#ifndef LF2_GPU_SHADER_SOURCE_H
#define LF2_GPU_SHADER_SOURCE_H

#include <SDL3/SDL_gpu.h>
#include <stddef.h>
#include <stdio.h>

/* SDL's GPU backends accept different shader languages. Vulkan consumes SPIR-V while Metal
 * consumes MSL; selecting only one at build time makes the whole native renderer disappear on
 * the other backend. Keep the choice in one place so the engine and dormant standalone mesh
 * pass cannot drift back to different platform support. */
typedef struct {
    const unsigned char *code;
    size_t code_size;
    const char *entrypoint;
    SDL_GPUShaderFormat format;
} GPUShaderSource;

static inline int gpu_shader_source_select(SDL_GPUShaderFormat supported, const unsigned char *spirv, size_t spirv_size,
                                           const unsigned char *msl, size_t msl_size, GPUShaderSource *out)
{
    *out = (GPUShaderSource){0};
    if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) && spirv && spirv_size) {
        out->code = spirv;
        out->code_size = spirv_size;
        out->entrypoint = "main";
        out->format = SDL_GPU_SHADERFORMAT_SPIRV;
        return 1;
    }
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) && msl && msl_size) {
        out->code = msl;
        out->code_size = msl_size;
        out->entrypoint = "main0";
        out->format = SDL_GPU_SHADERFORMAT_MSL;
        return 1;
    }
    return 0;
}

static inline const char *gpu_shader_format_name(SDL_GPUShaderFormat format)
{
    if (format == SDL_GPU_SHADERFORMAT_SPIRV) return "SPIR-V";
    if (format == SDL_GPU_SHADERFORMAT_MSL) return "MSL";
    return "unsupported";
}

/* Compile one shader from the committed payloads. Shared by the engine's pipelines and the
 * lighting chain so the error text and the payload selection cannot drift apart. */
static inline SDL_GPUShader *gpu_shader_make(SDL_GPUDevice *dev, SDL_GPUShaderFormat supported,
                                             const unsigned char *spirv, size_t spirv_size,
                                             const unsigned char *msl, size_t msl_size,
                                             SDL_GPUShaderStage stage, int samplers, int uniforms,
                                             const char *what)
{
    GPUShaderSource source;
    if (!gpu_shader_source_select(supported, spirv, spirv_size, msl, msl_size, &source)) {
        fprintf(stderr, "engine: no shader payload matches the %s backend for %s\n",
                SDL_GetGPUDeviceDriver(dev), what);
        return NULL;
    }
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code = source.code;
    info.code_size = source.code_size;
    info.entrypoint = source.entrypoint;
    info.format = source.format;
    info.stage = stage;
    info.num_samplers = (Uint32)samplers;
    info.num_uniform_buffers = (Uint32)uniforms;
    SDL_GPUShader *s = SDL_CreateGPUShader(dev, &info);
    if (!s) fprintf(stderr, "engine: the %s shader failed: %s\n", what, SDL_GetError());
    return s;
}

#endif
