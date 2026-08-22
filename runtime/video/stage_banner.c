#include "stage_banner.h"

enum {
    NATIVE_WIDTH = 794,
    BANNER_SOURCE_HEIGHT = 600,
    BANNER_TOP_MIN = 294,
    BANNER_TOP_MAX = 341,
};

int stage_banner_offset(int match_up, int view_w, int source_w, int source_h, int top)
{
    if (!match_up || view_w <= NATIVE_WIDTH) return 0;
    if (source_w != NATIVE_WIDTH || source_h != BANNER_SOURCE_HEIGHT) return 0;
    if (top < BANNER_TOP_MIN || top > BANNER_TOP_MAX) return 0;
    return (view_w - NATIVE_WIDTH) / 2;
}
