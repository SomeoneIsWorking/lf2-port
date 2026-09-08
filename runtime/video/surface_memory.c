#include "surface_memory.h"
#include "guest_arena.h"
#include "guest_map.h"
#include "lf2_log.h"
static GuestArena surfaces = {GUEST_VRAM_BASE, GUEST_VRAM_END, 4096};
static uint64_t allocation_count, requested_bytes;
uint32_t vram_alloc(uint32_t bytes)
{
    const uint32_t address = guest_arena_alloc(&surfaces, bytes);
    ++allocation_count;
    requested_bytes += bytes;
    return address;
}
void vram_report(void)
{
    lf2_log_writef(LF2_LOG_INFO, "surfaces", "vram: %llu allocations, %llu KB requested, %u KB of arena used",
                   (unsigned long long)allocation_count, (unsigned long long)(requested_bytes / 1024),
                   (surfaces.next - GUEST_VRAM_BASE) / 1024);
}
