/* Shared device artwork, embedded by CMake from port-assets.
 *
 * This module is the one authority for resolving and rasterising those assets. The in-game
 * indicators and RmlUi both consume it, so neither owns a second SVG-name table or decoder.
 */
#ifndef LF2_DEVICE_ASSETS_H
#define LF2_DEVICE_ASSETS_H

struct SDL_Surface;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DeviceAsset {
    DEVICE_ASSET_INVALID = -1,
    DEVICE_ASSET_KEYBOARD,
    DEVICE_ASSET_GAMEPAD,
    DEVICE_ASSET_COUNT
} DeviceAsset;

DeviceAsset device_asset_from_source(const char *source);

/* Returns a newly owned ARGB8888 surface. Zero width/height preserves the SVG's intrinsic
 * size; otherwise it is rasterised into exactly the requested pixel dimensions. */
struct SDL_Surface *device_asset_rasterize(DeviceAsset asset, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
