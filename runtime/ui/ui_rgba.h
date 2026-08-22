#ifndef LF2_UI_RGBA_H
#define LF2_UI_RGBA_H

#include <stdint.h>

/* Host UI rasters use straight-alpha ARGB8888. GPU tiles require premultiplied ARGB,
 * while the guest composition is opaque XRGB and requires straight source-over. */
uint32_t ui_rgba_premultiply(uint32_t source);
uint32_t ui_rgba_over_xrgb(uint32_t source, uint32_t destination);

#endif
