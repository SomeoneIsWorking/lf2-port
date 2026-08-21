#ifndef LF2_TEXRECT_H
#define LF2_TEXRECT_H

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

/* Match DirectDraw's integer stretch map exactly.  At destination pixel p, the software
 * compositor selects floor(p * source_extent / destination_extent).  GPU interpolation
 * happens at p + 0.5, so shift the texture interval back by half a source step.  The small
 * positive bias keeps exact integer boundaries on their intended texel; it is less than the
 * smallest non-zero fractional step (1 / destination_extent), so it cannot reach the next
 * texel. */
static inline void texrect_stretch(float x, float y, float w, float h, int destination_w, int destination_h,
                                   int sheet_w, int sheet_h, float *u0, float *v0, float *u1, float *v1)
{
    const float x_bias = 0.25f / (float)destination_w;
    const float y_bias = 0.25f / (float)destination_h;
    const float left = x - 0.5f * w / (float)destination_w + x_bias;
    const float top = y - 0.5f * h / (float)destination_h + y_bias;
    *u0 = left / (float)sheet_w;
    *v0 = top / (float)sheet_h;
    *u1 = (left + w) / (float)sheet_w;
    *v1 = (top + h) / (float)sheet_h;
}

static inline void texrect_for_blit(float x, float y, float w, float h, int destination_w, int destination_h,
                                    int sheet_w, int sheet_h, int shared_edge, float *u0, float *v0, float *u1,
                                    float *v1)
{
    const int whole = x == 0.0f && y == 0.0f && w == (float)sheet_w && h == (float)sheet_h;
    if (shared_edge || (whole && w == (float)destination_w && h == (float)destination_h)) {
        *u0 = x / (float)sheet_w;
        *v0 = y / (float)sheet_h;
        *u1 = (x + w) / (float)sheet_w;
        *v1 = (y + h) / (float)sheet_h;
    } else if (w != (float)destination_w || h != (float)destination_h) {
        texrect_stretch(x, y, w, h, destination_w, destination_h, sheet_w, sheet_h, u0, v0, u1, v1);
    } else {
        texrect_centres(x, y, w, h, sheet_w, sheet_h, u0, v0, u1, v1);
    }
}

#endif
