#include "ui_rgba.h"

uint32_t ui_rgba_premultiply(uint32_t source)
{
    const unsigned alpha = source >> 24;
    const unsigned red = (source >> 16) & 255u;
    const unsigned green = (source >> 8) & 255u;
    const unsigned blue = source & 255u;
    return (alpha << 24) | (((red * alpha + 127u) / 255u) << 16) | (((green * alpha + 127u) / 255u) << 8) |
           ((blue * alpha + 127u) / 255u);
}

uint32_t ui_rgba_over_xrgb(uint32_t source, uint32_t destination)
{
    const unsigned alpha = source >> 24;
    const unsigned inverse = 255u - alpha;
    const unsigned red = (((source >> 16) & 255u) * alpha + ((destination >> 16) & 255u) * inverse + 127u) / 255u;
    const unsigned green = (((source >> 8) & 255u) * alpha + ((destination >> 8) & 255u) * inverse + 127u) / 255u;
    const unsigned blue = ((source & 255u) * alpha + (destination & 255u) * inverse + 127u) / 255u;
    return (red << 16) | (green << 8) | blue;
}
