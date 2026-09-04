#include "lf2_log.h"
#include "device_icons.h"

#include "device_assets.h"
#include "guest.h"
#include "hostwin.h"
#include "render.h"
#include "ui_rgba.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static SDL_Surface *icons[DEVICE_ASSET_COUNT];
static SDL_Surface *native_icons[DEVICE_ASSET_COUNT];
static int load_failed[DEVICE_ASSET_COUNT];
static int native_failed[DEVICE_ASSET_COUNT];
static int native_scale_x100 = -1;

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
            lf2_log_writef(LF2_LOG_INFO, "device_icons", "device icons: could not rasterise embedded %s SVG: %s\n",
                           asset == DEVICE_ASSET_KEYBOARD ? "keyboard" : "gamepad", SDL_GetError());
        }
    }
    return icons[asset];
}

static void native_icons_reset(int scale_x100)
{
    for (int i = 0; i < DEVICE_ASSET_COUNT; i++) {
        if (native_icons[i]) SDL_DestroySurface(native_icons[i]);
        native_icons[i] = NULL;
        native_failed[i] = 0;
    }
    native_scale_x100 = scale_x100;
}

static SDL_Surface *native_icon_surface(DeviceAsset asset, float scale)
{
    if (asset < 0 || asset >= DEVICE_ASSET_COUNT) return NULL;
    const int key = (int)(scale * 100.0f + 0.5f);
    if (key != native_scale_x100) native_icons_reset(key);
    if (!native_icons[asset] && !native_failed[asset]) {
        /* Follow the renderer's existing high-resolution text path: rasterise at the final
         * output footprint, while the quad stays 18 composition pixels. The host-tile sampler
         * preserves the SVG's coverage when fractional scaling needs a small resample.
         * Re-rasterise when a resize changes scale. */
        int pixels = (int)((float)DEVICE_ICON_SIZE * scale + 0.5f);
        if (pixels < 1) pixels = 1;
        native_icons[asset] = device_asset_rasterize(asset, pixels, pixels);
        if (!native_icons[asset]) native_failed[asset] = 1;
    }
    return native_icons[asset];
}

int device_icon_paint(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y, int dev)
{
    SDL_Surface *icon = icon_surface(asset_for_device(dev));
    if (!icon || !g_mem) return 0;

    for (int iy = 0; iy < icon->h; iy++) {
        const uint32_t *src =
            (const uint32_t *)((const unsigned char *)icon->pixels + (size_t)iy * (size_t)icon->pitch);
        const int dy = y + iy;
        for (int ix = 0; ix < icon->w; ix++) {
            const int dx = x + ix;
            if (dx < 0 || dx >= dst_w || dy < 0 || dy >= dst_h) continue;
            uint32_t *dst = (uint32_t *)(g_mem + dst_pixels + (size_t)dy * (size_t)dst_pitch) + dx;
            *dst = ui_rgba_over_xrgb(src[ix], *dst);
        }
    }
    return 1;
}

int device_icon_record(uint32_t dst_pixels, int x, int y, int dev)
{
    SDL_Surface *icon = native_icon_surface(asset_for_device(dev), lf2_world_scale());
    if (!icon) return 0;
    uint32_t *tile = render_tile_begin(dst_pixels, x, y, DEVICE_ICON_SIZE, DEVICE_ICON_SIZE, icon->w, icon->h);
    if (!tile) return 0;
    for (int iy = 0; iy < icon->h; iy++) {
        const uint32_t *src =
            (const uint32_t *)((const unsigned char *)icon->pixels + (size_t)iy * (size_t)icon->pitch);
        for (int ix = 0; ix < icon->w; ix++)
            tile[(size_t)iy * (size_t)icon->w + (size_t)ix] = ui_rgba_premultiply(src[ix]);
    }
    render_tile_end();
    return 1;
}

void device_icons_shutdown(void)
{
    for (int i = 0; i < DEVICE_ASSET_COUNT; i++) {
        if (icons[i]) SDL_DestroySurface(icons[i]);
        if (native_icons[i]) SDL_DestroySurface(native_icons[i]);
        icons[i] = NULL;
        native_icons[i] = NULL;
        load_failed[i] = 0;
        native_failed[i] = 0;
    }
    native_scale_x100 = -1;
}
