#include "cheats.h"
#include "guest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MEM_SIZE = 0x500000, STACK = 0x1000, EVENT = 0x2000, STATE = 0x3000 };

Cpu cpu;
uint8_t *g_mem;
uint32_t g_rwatch_lo, g_rwatch_hi;

static uint32_t requested_vk;

void rwatch_hit(uint32_t address) { (void)address; }
int function_key_request(uint32_t vk)
{
    requested_vk = vk;
    return 1;
}
void function_keys_tick(void) {}
int hostwin_injected_key(uint32_t vk)
{
    (void)vk;
    return 0;
}

void fn_00416e10(void);
void fn_00416e60(void);
void fn_00416eb0(void);
void fn_00416f10(void);
void fn_00416f60(void);

static void stack3(uint32_t event, uint32_t screen, uint32_t mode)
{
    R(ESP) = STACK;
    *(uint32_t *)(g_mem + STACK + 4) = event;
    *(uint32_t *)(g_mem + STACK + 8) = screen;
    *(uint32_t *)(g_mem + STACK + 12) = mode;
}

int main(void)
{
    g_mem = calloc(1, MEM_SIZE);
    assert(g_mem);

    size_t count = 0;
    const CheatDescriptor *entries = cheats_descriptors(&count);
    assert(count == 5);
    assert(entries[0].action == CHEAT_SECRET_FIGHTERS && !entries[0].match_only);
    for (size_t i = 1; i < count; ++i) {
        assert(entries[i].action == (CheatAction)i);
        assert(entries[i].vk == 0x74u + i);
        assert(entries[i].match_only);
    }
    CheatAction action;
    assert(cheats_action_from_id("restore", &action) && action == CHEAT_RESTORE);
    assert(!cheats_action_from_id("pause", &action));

    assert(cheats_request(CHEAT_SECRET_FIGHTERS));
    assert(*(uint32_t *)(g_mem + 0x458428) == 1);
    assert(cheats_request(CHEAT_DROP_ITEMS) && requested_vk == 0x77);
    assert(*(uint32_t *)(g_mem + 0x450c28) == 0);

    /* F3 keeps its replay bit but can no longer create the lock state. */
    stack3(EVENT, STATE, 0);
    *(uint32_t *)(g_mem + STATE) = 0;
    *(uint32_t *)(g_mem + 0x450c28) = 2;
    fn_00416e10();
    assert(R(ESP) == STACK + 4);
    assert((g_mem[EVENT + 8] & 4) != 0);
    assert(*(uint32_t *)(g_mem + 0x450c28) == 0);

    /* Mode is deliberately 1 (Stage) with HEROFIGHTER.COM off: the native ports remove
     * that hidden gate while preserving every actual cheat effect. */
    *(uint32_t *)(g_mem + 0x44d020) = 0;
    *(uint32_t *)(g_mem + 0x450bdc) = 0;
    *(uint32_t *)(g_mem + 0x45842c) = 0;

    stack3(EVENT, 0x44d020, STATE);
    *(uint32_t *)(g_mem + STATE) = 1;
    fn_00416e60();
    assert(*(uint32_t *)(g_mem + 0x44d034) == 1);
    assert(*(uint32_t *)(g_mem + 0x450c18) == 1);

    stack3(EVENT, 0x44d020, STATE);
    fn_00416eb0();
    assert(*(uint32_t *)(g_mem + 0x450bc0) == 1);
    assert(*(uint32_t *)(g_mem + 0x450c1c) == 1);

    stack3(EVENT, 0x44d020, STATE);
    fn_00416f10();
    assert(*(uint32_t *)(g_mem + 0x450c20) == 1);
    assert(*(uint32_t *)(g_mem + 0x450bb8) == 1);

    stack3(EVENT, 0x44d020, STATE);
    fn_00416f60();
    assert(*(uint32_t *)(g_mem + 0x450c24) == 1);
    assert(*(uint32_t *)(g_mem + 0x450bb8) == 2);

    free(g_mem);
    puts("cheat descriptors and native leaves: ok");
    return 0;
}
