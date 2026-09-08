#include "guest_arena.h"
#include "guest_memory.h"
#include "lf2_log.h"
#include <stdlib.h>

uint32_t guest_arena_alloc(GuestArena *arena, uint32_t bytes)
{
    const uint32_t alignment = arena->alignment;
    const uint64_t requested = bytes ? bytes : 1;
    const uint64_t rounded = (requested + alignment - 1) & ~((uint64_t)alignment - 1);
    if (!alignment || (alignment & (alignment - 1)) || arena->next % alignment || arena->next > arena->end ||
        rounded > (uint64_t)arena->end - arena->next) {
        lf2_log_writef(LF2_LOG_ERROR, "guest-arena", "allocation refused: bytes=%u next=%08x end=%08x alignment=%u",
                       bytes, arena->next, arena->end, alignment);
        abort();
    }
    const uint32_t address = arena->next;
    guest_memory_map(address, (uint32_t)rounded);
    arena->next += (uint32_t)rounded;
    return address;
}
