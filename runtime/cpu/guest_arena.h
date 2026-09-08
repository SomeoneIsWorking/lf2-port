#ifndef LF2_GUEST_ARENA_H
#define LF2_GUEST_ARENA_H
#include <stdint.h>
/* Bounded, aligned guest addresses; backing is published before returning.
 * Allocations survive until guest-memory shutdown, matching the title's existing
 * session arenas. This owner does not implement a reclaiming heap. */
typedef struct {
    uint32_t next;
    uint32_t end;
    uint32_t alignment;
} GuestArena;
uint32_t guest_arena_alloc(GuestArena *arena, uint32_t bytes);
#endif
