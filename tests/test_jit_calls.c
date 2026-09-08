#include "guest.h"
#include "jit_executor.h"
#include "native_override.h"

#include <stdio.h>
#include <string.h>

Cpu cpu;
uint32_t g_rwatch_lo, g_rwatch_hi;

static uint8_t bytes[65536] = {
    /* MOV EAX,IMPORT_SENTINEL; CALL EAX; ADD EAX,3; RET. */
    [0x1000] = 0xb8,
    0,
    0,
    0,
    0xf0,
    0xff,
    0xd0,
    0x83,
    0xc0,
    3,
    0xc3,
    /* MOV EAX,10; RET. */
    [0x2000] = 0xb8,
    10,
    0,
    0,
    0,
    0xc3,
    [0x3000] = 0xcc,
    [0x4000] = 0xcc,
};
static unsigned imported_calls;
static unsigned overridden_calls;

void rwatch_hit(uint32_t address)
{
    (void)address;
    abort();
}

static void original_override(void)
{
    ++overridden_calls;
    lf2_jit_call_original(0x2000);
}

Lf2NativeOverride lf2_native_override_find(uint32_t address, uint32_t excluded)
{
    if (address == excluded) return NULL;
    return address == 0x2000 ? original_override : NULL;
}

void com_call(uint32_t sentinel)
{
    (void)sentinel;
    abort();
}

void host_import(uint32_t sentinel)
{
    if (sentinel != IMPORT_SENTINEL) abort();
    ++imported_calls;
    PUSH32(0x3000);
    lf2_jit_call(0x2000);
    R(ESP) += 4;
}

static void call(uint32_t entry)
{
    cpu = (Cpu){0};
    R(ESP) = 0xf000;
    PUSH32(0x4000);
    lf2_jit_call(entry);
}

static int check_call(uint32_t expected)
{
    if (R(EAX) != expected || R(ESP) != 0xf000 || cpu.eip != 0x4000) {
        printf("FAIL call: eax=%u esp=%08x eip=%08x\n", R(EAX), R(ESP), cpu.eip);
        return 1;
    }
    return 0;
}

int main(void)
{
    guest_memory_init();
    guest_memory_map(0, sizeof bytes);
    memcpy(guest_write_pointer(0, sizeof bytes), bytes, sizeof bytes);
    call(0x2000);
    if (check_call(10)) return 1;
    call(0x1000);
    if (check_call(13)) return 1;
    call(0x1000);
    if (check_call(13)) return 1;
    if (imported_calls != 2 || overridden_calls != 3) {
        printf("FAIL: imported=%u/2 overridden=%u/3\n", imported_calls, overridden_calls);
        return 1;
    }
    puts("PASS: 3 JIT calls, 2 nested HLE calls, 3 scoped originals; register, stack and return boundaries preserved");
    return 0;
}
