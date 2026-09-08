#include "guest.h"
#include "guest_arena.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>

Cpu cpu;
uint32_t g_rwatch_lo, g_rwatch_hi;
static unsigned writes;
static uint32_t observed_address;
static uint8_t observed_before;
void rwatch_hit(uint32_t address)
{
    (void)address;
    abort();
}
void lf2_jit_invalidate(uint32_t address, uint32_t length)
{
    ++writes;
    observed_address = address;
    uint8_t *host = guest_memory_resolve(address, length);
    observed_before = host ? *host : 0;
}
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(void)
{
    guest_memory_init();
    CHECK(g_mem == NULL && guest_memory_view()->sparse != NULL);
    GuestArena low = {0x20000000, 0x20000100, 16};
    GuestArena high = {0x90000000, 0x90001000, 4096};
    const uint32_t first = guest_arena_alloc(&low, 7);
    const uint32_t second = guest_arena_alloc(&low, 5);
    const uint32_t audio = guest_arena_alloc(&high, 20);
    CHECK(first == 0x20000000 && second == first + 16 && audio == 0x90000000);
    CHECK(guest_memory_resolve(first, 16) && !guest_memory_resolve(first, 17));
    CHECK(!guest_memory_resolve(first + 32, 1) && !guest_memory_resolve(UINT32_MAX, 2));
    CHECK(guest_memory_resolve(audio, 4096) && !guest_memory_resolve(audio + 4096, 1));
    writes = 0;
    ST32(first, 0x12345678);
    CHECK(writes == 1 && observed_address == first && observed_before == 0 && LD32(first) == 0x12345678);
    memcpy(guest_write_pointer(second, 6), "hello", 6);
    CHECK(strcmp(guest_string(second), "hello") == 0);
    ST32(audio, 0xabcdef01);
    CHECK(LD32(first) == 0x12345678 && LD32(audio) == 0xabcdef01);
    const uint8_t across[2] = {0xa5, 0x5a};
    CHECK(x86p_mem_write_bytes(guest_memory_view(), first + 15, across, 2));
    CHECK(LD8(first + 15) == 0xa5 && LD8(second) == 0x5a);
    uint8_t invalid[2] = {1, 2};
    CHECK(!x86p_mem_read_bytes(guest_memory_view(), second + 15, invalid, 2));
    CHECK(invalid[0] == 1 && invalid[1] == 2);
    guest_memory_shutdown();
    CHECK(!guest_memory_ready());
    puts("PASS: sparse arena backing, complete native spans, holes, address wrap, cross-allocation shared access and "
         "prewrite invalidation");
    return 0;
}
