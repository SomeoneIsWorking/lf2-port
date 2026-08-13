#ifndef LF2_TEXRECT_H
#define LF2_TEXRECT_H

/* A source rectangle names texels [x, x+w), but a normalized GPU coordinate exactly on x is
 * the boundary shared with texel x-1.  With nearest sampling, floating-point/raster rounding
 * can therefore select the neighbouring animation cell: issue #67's green menu line and
 * issue #68's stray eye beside a struck Bandit are the top and left forms of the same bug.
 * Address the centres of the first and last texels instead. */
static inline void texrect_centres(float x, float y, float w, float h,
                                   int sheet_w, int sheet_h,
                                   float *u0, float *v0, float *u1, float *v1)
{
    *u0 = (x + 0.5f) / (float)sheet_w;
    *v0 = (y + 0.5f) / (float)sheet_h;
    *u1 = (x + w - 0.5f) / (float)sheet_w;
    *v1 = (y + h - 0.5f) / (float)sheet_h;
}

#endif
