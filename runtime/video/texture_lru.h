#ifndef LF2_TEXTURE_LRU_H
#define LF2_TEXTURE_LRU_H

#include <stdint.h>

typedef struct {
    uint64_t last_frame;
} TextureLruEntry;

/* An entry touched in current_frame may already be referenced by the GPU command buffer being
 * assembled, so it is never a legal victim. Among older entries, replace the least recently
 * used. This policy is kept pure so the shipping cache and the offline falsifier share it. */
static inline int texture_lru_choose(const TextureLruEntry *entries, int count, uint64_t current_frame)
{
    int victim = -1;
    for (int i = 0; i < count; i++) {
        if (entries[i].last_frame == current_frame) continue;
        if (victim < 0 || entries[i].last_frame < entries[victim].last_frame) victim = i;
    }
    return victim;
}

#endif
