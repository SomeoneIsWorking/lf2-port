#ifndef LF2_BACKDROP_LAYOUT_H
#define LF2_BACKDROP_LAYOUT_H

#include <string.h>

/* All Lion Forest layers keep their shared authored world origin. Different spans are parallax
 * metadata, not independent canvases: centring 800/1100/1400 separately breaks the registration
 * baked into their pixels. Only the opaque far sheet is explicitly authored to continue, by
 * mirroring its right edge behind every keyed mountain piece. */
typedef struct {
    const char *stage;
    int span;
    int x;
    int flags;
} BackdropPlanePolicy;

static inline int backdrop_plane_placement(const char *stage, int span, int x, int view, int *translation, int *flags)
{
    static const BackdropPlanePolicy authored[] = {
        {"Lion_Forest", 800, 0, BACKDROP_MIRROR_RIGHT | BACKDROP_EXTEND_BOTTOM},
        {"Lion_Forest", 1100, 0, 0},
        {"Lion_Forest", 1100, 800, BACKDROP_MIRROR_RIGHT},
        {"Lion_Forest", 1400, 0, 0},
        {"Lion_Forest", 1400, 1216, BACKDROP_MIRROR_RIGHT},
    };
    if (!translation || !flags) return 0;
    *translation = 0;
    *flags = 0;
    if (!stage || span <= 0 || view <= 794) return 0;
    for (unsigned i = 0; i < sizeof authored / sizeof authored[0]; i++) {
        if (span != authored[i].span || x != authored[i].x || strcmp(stage, authored[i].stage) != 0) continue;
        *flags = authored[i].flags;
        return 1;
    }
    return 0;
}

#endif
