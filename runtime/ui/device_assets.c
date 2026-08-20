#include "device_assets.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <stddef.h>
#include <string.h>

extern const unsigned char lf2_device_keyboard_svg[];
extern const unsigned int lf2_device_keyboard_svg_len;
extern const unsigned char lf2_device_gamepad_svg[];
extern const unsigned int lf2_device_gamepad_svg_len;

static int has_suffix(const char *source, const char *suffix)
{
    if (!source || !suffix) return 0;
    const size_t source_n = strlen(source), suffix_n = strlen(suffix);
    return source_n >= suffix_n && strcmp(source + source_n - suffix_n, suffix) == 0;
}

DeviceAsset device_asset_from_source(const char *source)
{
    if (has_suffix(source, "device_keyboard.svg")) return DEVICE_ASSET_KEYBOARD;
    if (has_suffix(source, "device_gamepad.svg")) return DEVICE_ASSET_GAMEPAD;
    return DEVICE_ASSET_INVALID;
}

static int source_bytes(DeviceAsset asset, const unsigned char **data, size_t *size)
{
    if (asset == DEVICE_ASSET_KEYBOARD) {
        *data = lf2_device_keyboard_svg;
        *size = lf2_device_keyboard_svg_len;
        return 1;
    }
    if (asset == DEVICE_ASSET_GAMEPAD) {
        *data = lf2_device_gamepad_svg;
        *size = lf2_device_gamepad_svg_len;
        return 1;
    }
    return 0;
}

SDL_Surface *device_asset_rasterize(DeviceAsset asset, int width, int height)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    if (!source_bytes(asset, &data, &size)) return NULL;

    SDL_IOStream *stream = SDL_IOFromConstMem(data, size);
    SDL_Surface *loaded = stream ? IMG_LoadTyped_IO(stream, true, "svg") : NULL;
    if (!loaded) return NULL;

    SDL_Surface *argb = loaded->format == SDL_PIXELFORMAT_ARGB8888
                      ? loaded : SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    if (argb != loaded) SDL_DestroySurface(loaded);
    if (!argb) return NULL;
    if (width <= 0 || height <= 0 || (argb->w == width && argb->h == height)) return argb;

    SDL_Surface *scaled = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled) { SDL_DestroySurface(argb); return NULL; }
    SDL_SetSurfaceBlendMode(argb, SDL_BLENDMODE_NONE);
    const SDL_Rect dst = { 0, 0, width, height };
    if (!SDL_BlitSurfaceScaled(argb, NULL, scaled, &dst, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(scaled);
        scaled = NULL;
    }
    SDL_DestroySurface(argb);
    return scaled;
}
