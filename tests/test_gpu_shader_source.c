/* Every renderer shader must have both payloads. This is an offline gate over the exact arrays
 * engine.c and mesh.c include: a Metal selector that returns MSL while one shadow shader is
 * still SPIR-V-only would merely move the macOS failure from engine_init to pipeline creation. */
#include "video/gpu_shader_source.h"

#include "shaders/gen/quad_vert_spv.h"
#include "shaders/gen/quad_spv.h"
#include "shaders/gen/mesh_vert_spv.h"
#include "shaders/gen/mesh_spv.h"
#include "shaders/gen/hd2d_quad_vert_spv.h"
#include "shaders/gen/hd2d_character_spv.h"
#include "shaders/gen/hd2d_shadow_spv.h"
#include "shaders/gen/hd2d_light_spv.h"

#include "shaders/gen/quad_vert_msl.h"
#include "shaders/gen/quad_msl.h"
#include "shaders/gen/mesh_vert_msl.h"
#include "shaders/gen/mesh_msl.h"
#include "shaders/gen/hd2d_quad_vert_msl.h"
#include "shaders/gen/hd2d_character_msl.h"
#include "shaders/gen/hd2d_shadow_msl.h"
#include "shaders/gen/hd2d_light_msl.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void ok(const char *what, int condition)
{
    checks++;
    if (condition) return;
    failures++;
    printf("  FAIL  %s\n", what);
}

static int contains(const unsigned char *bytes, size_t size, const char *needle)
{
    const size_t n = strlen(needle);
    if (n > size) return 0;
    for (size_t i = 0; i <= size - n; i++)
        if (memcmp(bytes + i, needle, n) == 0) return 1;
    return 0;
}

#define CHECK_SHADER(name) check_shader(#name, name##_spv, sizeof name##_spv, name##_msl, sizeof name##_msl)

static void check_shader(const char *name, const unsigned char *spirv, size_t spirv_size, const unsigned char *msl,
                         size_t msl_size)
{
    GPUShaderSource source;
    char what[160];

    snprintf(what, sizeof what, "%s selects MSL on a Metal-only backend", name);
    ok(what, gpu_shader_source_select(SDL_GPU_SHADERFORMAT_MSL, spirv, spirv_size, msl, msl_size, &source) &&
                 source.format == SDL_GPU_SHADERFORMAT_MSL);
    snprintf(what, sizeof what, "%s's Metal entrypoint is main0", name);
    ok(what, source.entrypoint && strcmp(source.entrypoint, "main0") == 0);
    snprintf(what, sizeof what, "%s ships nonempty MSL source containing main0", name);
    ok(what, source.code && source.code_size == msl_size && contains(source.code, source.code_size, "main0"));

    snprintf(what, sizeof what, "%s preserves SPIR-V on Vulkan", name);
    ok(what, gpu_shader_source_select(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, spirv, spirv_size, msl,
                                      msl_size, &source) &&
                 source.format == SDL_GPU_SHADERFORMAT_SPIRV && source.entrypoint &&
                 strcmp(source.entrypoint, "main") == 0);
}

int main(void)
{
    CHECK_SHADER(quad_vert);
    CHECK_SHADER(quad);
    CHECK_SHADER(mesh_vert);
    CHECK_SHADER(mesh);
    CHECK_SHADER(hd2d_quad_vert);
    CHECK_SHADER(hd2d_character);
    CHECK_SHADER(hd2d_shadow);
    CHECK_SHADER(hd2d_light);

    GPUShaderSource source;
    ok("an unsupported backend is refused instead of receiving the wrong bytecode",
       !gpu_shader_source_select(0, quad_vert_spv, sizeof quad_vert_spv, quad_vert_msl, sizeof quad_vert_msl, &source));

    printf("GPU shader sources: %d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
