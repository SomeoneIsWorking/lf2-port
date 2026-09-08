#ifndef LF2_GDI_BITMAP_H
#define LF2_GDI_BITMAP_H

#include <stdint.h>

typedef struct {
    int w, h, pitch, bpp;
    uint8_t *pixels; /* top-down, one byte per pixel for 8-bit */
    uint32_t pal[256];
} Bitmap;

Bitmap *bitmap_load_file(const char *path);
Bitmap *bitmap_load_resource(const char *name);

#endif
