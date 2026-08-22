#ifndef LF2_RASTER_H
#define LF2_RASTER_H

/* Place one logical X coordinate on the full-resolution raster.
 *
 * Most game draws have no `phase`. Background layers are the exception: LF2 computes a
 * rational parallax position but its original 794x550 renderer truncates that position to an
 * integer before drawing. Reusing only that integer at a magnified output turns one native
 * pixel into a several-pixel jump. The background owner supplies the discarded fractional
 * remainder, and this shared transform lets both native renderers consume it.
 *
 * At 1x the phase is deliberately ignored. That is the game's authored raster contract and
 * keeps the native-width software/native comparison exact. Above 1x there are real output
 * fragments available to represent the remainder, so applying it is resolution rather than
 * temporal smoothing or camera quantisation. */
static inline float raster_place_x(float logical_x, float phase, float world, float scale, float output_x)
{
    const float resolved_x = logical_x + (scale > 1.0f ? phase : 0.0f);
    return (resolved_x + world) * scale + output_x;
}

#endif
