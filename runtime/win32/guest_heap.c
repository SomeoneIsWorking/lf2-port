#include "guest_heap.h"
#include "guest_arena.h"
#include "guest_map.h"
#include "guest_memory.h"
#include <string.h>
static GuestArena heap = {GUEST_HEAP_BASE, GUEST_HEAP_END, 16};
uint32_t guest_alloc(uint32_t bytes)
{
    return guest_arena_alloc(&heap, bytes);
}
uint32_t guest_heap_used(void)
{
    return heap.next - GUEST_HEAP_BASE;
}

uint32_t guest_calloc(uint32_t count, uint32_t size)
{
    const uint64_t bytes = (uint64_t)count * size;
    if (bytes > UINT32_MAX) return 0;
    const uint32_t address = guest_alloc((uint32_t)bytes);
    if (bytes) memset(guest_write_pointer(address, (size_t)bytes), 0, (size_t)bytes);
    return address;
}
