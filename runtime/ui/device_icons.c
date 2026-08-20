#include "device_icons.h"

#include "device_assets.h"
#include "guest.h"
#include "render.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static SDL_Surface *icons[DEVICE_ASSET_COUNT];
static int load_failed[DEVICE_ASSET_COUNT];

static DeviceAsset asset_for_device(int dev)
{
    if (dev == 0) return DEVICE_ASSET_KEYBOARD;
    if (dev >= 1 && dev <= 4) return DEVICE_ASSET_GAMEPAD;
    return DEVICE_ASSET_INVALID;
}

static SDL_Surface *icon_surface(DeviceAsset asset)
{
    if (asset < 0 || asset >= DEVICE_ASSET_COUNT) return NULL;
    if (!icons[asset] && !load_failed[asset]) {
        icons[asset] = device_asset_rasterize(asset, DEVICE_ICON_SIZE, DEVICE_ICON_SIZE);
        if (!icons[asset]) {
            load_failed[asset] = 1;
            fprintf(stderr, "device icons: could not rasterise embedded %s SVG: %s\n",
                    asset == DEVICE_ASSET_KEYBOARD ? "keyboard" : "gamepad", SDL_GetError());
        }
    }
    return icons[asset];
}

static uint32_t premultiply(uint32_t source)
{
    const unsigned a = source >> 24;
    const unsigned r = (source >> 16) & 255u;
    const unsigned g = (source >> 8) & 255u;
    const unsigned b = source & 255u;
    return (a << 24) | (((r * a + 127u) / 255u) << 16)
                     | (((g * a + 127u) / 255u) << 8)
                     | ((b * a + 127u) / 255u);
}

static uint32_t over(uint32_t source, uint32_t dest)
{
    const unsigned a = source >> 24;
    const unsigned inv = 255u - a;
    const unsigned r = (((source >> 16) & 255u) * a + ((dest >> 16) & 255u) * inv + 127u) / 255u;
    const unsigned g = (((source >> 8) & 255u) * a + ((dest >> 8) & 255u) * inv + 127u) / 255u;
    const unsigned b = ((source & 255u) * a + (dest & 255u) * inv + 127u) / 255u;
    return (r << 16) | (g << 8) | b;
}

int device_icon_paint(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch,
                      int x, int y, int dev)
{
    SDL_Surface *icon = icon_surface(asset_for_device(dev));
    if (!icon || !g_mem) return 0;

    for (int iy = 0; iy < icon->h; iy++) {
        const uint32_t *src = (const uint32_t *)((const unsigned char *)icon->pixels
                                                + (size_t)iy * (size_t)icon->pitch);
        const int dy = y + iy;
        for (int ix = 0; ix < icon->w; ix++) {
            const int dx = x + ix;
            if (dx < 0 || dx >= dst_w || dy < 0 || dy >= dst_h) continue;
            uint32_t *dst = (uint32_t *)(g_mem + dst_pixels + (size_t)dy * (size_t)dst_pitch)
                          + dx;
            *dst = over(src[ix], *dst);
        }
    }
    return 1;
}

int device_icon_record(uint32_t dst_pixels, int x, int y, int dev)
{
    SDL_Surface *icon = icon_surface(asset_for_device(dev));
    if (!icon) return 0;
    uint32_t *tile = render_tile_begin(dst_pixels, x, y, icon->w, icon->h, 0, 0);
    if (!tile) return 0;
    for (int iy = 0; iy < icon->h; iy++) {
        const uint32_t *src = (const uint32_t *)((const unsigned char *)icon->pixels
                                                + (size_t)iy * (size_t)icon->pitch);
        for (int ix = 0; ix < icon->w; ix++)
            tile[(size_t)iy * (size_t)icon->w + (size_t)ix] = premultiply(src[ix]);
    }
    render_tile_end();
    return 1;
}

void device_icons_shutdown(void)
{
    for (int i = 0; i < DEVICE_ASSET_COUNT; i++) {
        if (icons[i]) SDL_DestroySurface(icons[i]);
        icons[i] = NULL;
        load_failed[i] = 0;
    }
}
