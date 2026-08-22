/* Lifetime of the immutable frame shown while a match is frozen.
 *
 * The GPU texture itself belongs to render.c. This header owns the policy that can be tested
 * without SDL: only a successfully copied LIVE native frame is eligible, it belongs to one
 * composition source, and every frozen present is counted. Opening RmlUi never resurrects a
 * display list or any of its mutable tile backing (issue #94).
 */
#ifndef LF2_FRAME_SNAPSHOT_H
#define LF2_FRAME_SNAPSHOT_H

#include <stdint.h>

typedef struct {
    uint32_t source_pixels;
    int width, height;
    int valid;
    long frozen_frames;
} FrameSnapshot;

static inline void frame_snapshot_init(FrameSnapshot *s)
{
    s->source_pixels = 0;
    s->width = 0;
    s->height = 0;
    s->valid = 0;
    s->frozen_frames = 0;
}

static inline void frame_snapshot_invalidate(FrameSnapshot *s)
{
    s->source_pixels = 0;
    s->width = 0;
    s->height = 0;
    s->valid = 0;
}

static inline void frame_snapshot_captured(FrameSnapshot *s, uint32_t source_pixels, int width, int height, int copied)
{
    if (!copied || !source_pixels || width <= 0 || height <= 0) {
        frame_snapshot_invalidate(s);
        return;
    }
    s->source_pixels = source_pixels;
    s->width = width;
    s->height = height;
    s->valid = 1;
}

static inline int frame_snapshot_can_freeze(const FrameSnapshot *s, uint32_t source_pixels)
{ return s->valid && s->source_pixels == source_pixels; }

static inline void frame_snapshot_presented_frozen(FrameSnapshot *s) { s->frozen_frames++; }

/* Aspect-preserving placement of an immutable raster into a resized output. Stretching it
 * to the new aspect would turn a pause into issue #87's class of geometry distortion. */
static inline void frame_snapshot_contain(int source_w, int source_h, int output_w, int output_h, float *x, float *y,
                                          float *w, float *h)
{
    if (source_w <= 0 || source_h <= 0 || output_w <= 0 || output_h <= 0) {
        *x = *y = *w = *h = 0.0f;
        return;
    }
    const float sx = (float)output_w / (float)source_w;
    const float sy = (float)output_h / (float)source_h;
    const float scale = sx < sy ? sx : sy;
    *w = (float)source_w * scale;
    *h = (float)source_h * scale;
    *x = ((float)output_w - *w) * 0.5f;
    *y = ((float)output_h - *h) * 0.5f;
}

#endif
