#ifndef LF2_TEXRECT_H
#define LF2_TEXRECT_H

#include <math.h>

/* A source rectangle names texels [x, x+w), but a normalized GPU coordinate exactly on x is
 * the boundary shared with texel x-1.  With nearest sampling, floating-point/raster rounding
 * can therefore select the neighbouring animation cell on a scaled output: issue #67's green
 * menu line and issue #68's stray eye beside a struck fighter are two forms of the same bug.
 * Address the centres of the first and last texels instead. */
static inline void texrect_centres(float x, float y, float w, float h, int sheet_w, int sheet_h, float *u0, float *v0,
                                   float *u1, float *v1)
{
    *u0 = (x + 0.5f) / (float)sheet_w;
    *v0 = (y + 0.5f) / (float)sheet_h;
    *u1 = (x + w - 0.5f) / (float)sheet_w;
    *v1 = (y + h - 0.5f) / (float)sheet_h;
}

static inline void texrect_axis_centres(float start, float extent, int sheet_extent, float *first, float *last)
{
    *first = (start + 0.5f) / (float)sheet_extent;
    *last = (start + extent - 0.5f) / (float)sheet_extent;
}

/* Match one full-resolution stretch axis to the raster fragments the output rectangle
 * actually covers. DirectDraw's source/destination comparison still decides WHETHER this is
 * a stretch; once it is, the native renderer stretches once at output resolution rather than
 * first reducing to the logical composition and magnifying that result.
 *
 * The first covered pixel centre is not necessarily output_start+0.5 when placement is
 * fractional. Deriving the phase and covered count from the rectangle keeps classic and
 * engine sampling identical at awkward output offsets instead of silently shifting one
 * source transition. */
static inline void texrect_axis_output_stretch(float source_start, float source_extent, float output_start,
                                               float output_extent, int sheet_extent, float *first, float *last)
{
    const int first_pixel = (int)ceilf(output_start - 0.5f);
    const int end_pixel = (int)ceilf(output_start + output_extent - 0.5f);
    const int raster_extent = end_pixel - first_pixel;
    if (raster_extent <= 0) {
        texrect_axis_centres(source_start, source_extent, sheet_extent, first, last);
        return;
    }
    const float source_step = source_extent / (float)raster_extent;
    const float first_centre = (float)first_pixel + 0.5f;
    const float bias = 0.25f / (float)raster_extent;
    const float source_first = source_start - (first_centre - output_start) * source_step + bias;
    *first = source_first / (float)sheet_extent;
    *last = (source_first + output_extent * source_step) / (float)sheet_extent;
}

static inline void texrect_axis_for_output(float source_start, float source_extent, int logical_extent,
                                           float output_start, float output_extent, int sheet_extent, int shared_edge,
                                           float *first, float *last)
{
    const int whole = source_start == 0.0f && source_extent == (float)sheet_extent;
    if (shared_edge || (whole && source_extent == (float)logical_extent)) {
        *first = source_start / (float)sheet_extent;
        *last = (source_start + source_extent) / (float)sheet_extent;
    } else if (source_extent != (float)logical_extent) {
        texrect_axis_output_stretch(source_start, source_extent, output_start, output_extent, sheet_extent, first,
                                    last);
    } else {
        texrect_axis_centres(source_start, source_extent, sheet_extent, first, last);
    }
}

/* DirectDraw's logical source/destination rectangles decide whether each AXIS is a 1:1
 * sprite copy or a true StretchBlt. A 1:1 axis addresses source texel centres and stays
 * invariant under output magnification. A stretched axis maps once across the output raster.
 * Passing the output rectangle as well as the logical extent is the unit boundary issue #96
 * crossed: neither extent can substitute for the other. */
static inline void texrect_for_output_blit(float x, float y, float w, float h, int logical_w, int logical_h,
                                           float output_x, float output_y, float output_w, float output_h, int sheet_w,
                                           int sheet_h, int shared_edge, float *u0, float *v0, float *u1, float *v1)
{
    texrect_axis_for_output(x, w, logical_w, output_x, output_w, sheet_w, shared_edge, u0, u1);
    texrect_axis_for_output(y, h, logical_h, output_y, output_h, sheet_h, shared_edge, v0, v1);
}

static inline void texrect_for_blit(float x, float y, float w, float h, int destination_w, int destination_h,
                                    int sheet_w, int sheet_h, int shared_edge, float *u0, float *v0, float *u1,
                                    float *v1)
{
    texrect_for_output_blit(x, y, w, h, destination_w, destination_h, 0.0f, 0.0f, (float)destination_w,
                            (float)destination_h, sheet_w, sheet_h, shared_edge, u0, v0, u1, v1);
}

#endif
