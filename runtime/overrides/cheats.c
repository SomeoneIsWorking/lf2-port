/* LF2's hidden roster and four built-in gameplay cheats.
 *
 * fn_0041bc90 consumes VK_F6..VK_F9 and dispatches the game's own actions. Keeping that
 * dispatch authoritative preserves its replay flags, counters, and world effects. The
 * binary normally hides the actions outside VS unless HEROFIGHTER.COM was typed, and F3
 * permanently locks them for the match. Native leaf ports remove both restrictions while
 * retaining screen, countdown, event-mask, action, and usage-counter behavior.
 */
#include "cheats.h"

#include "function_keys.h"
#include "guest_ops.h"
#include "hostwin.h"

#include <stdio.h>
#include <string.h>

enum {
    VK_F6 = 0x75,
    GX_FUNCTION_KEY_STATE = 0x00450c28,
    GX_SECRET_FIGHTERS = 0x00458428,
    GX_SCREEN = 0x0044d020,
    GX_COUNTDOWN = 0x00450bdc
};

static const CheatDescriptor DESCRIPTORS[] = {
    {CHEAT_SECRET_FIGHTERS, "secret_fighters", "LF2.NET", "Unlock secret fighters",
     "Reveal the original game's hidden roster.", 0, 0},
    {CHEAT_UNLIMITED_MP, "unlimited_mp", "F6", "Unlimited MP", "Toggle MP consumption for every fighter.", VK_F6, 1},
    {CHEAT_RESTORE, "restore", "F7", "Restore fighters", "Restore every fighter's HP and MP.", VK_F6 + 1, 1},
    {CHEAT_DROP_ITEMS, "drop_items", "F8", "Drop items", "Drop weapons and items into the stage.", VK_F6 + 2, 1},
    {CHEAT_DESTROY_ITEMS, "destroy_items", "F9", "Destroy items", "Remove weapons and items from the stage.", VK_F6 + 3,
     1},
};

static unsigned activations[CHEAT_ACTION_COUNT];

const CheatDescriptor *cheats_descriptors(size_t *count)
{
    if (count) *count = sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);
    return DESCRIPTORS;
}

int cheats_action_from_id(const char *id, CheatAction *action)
{
    if (!id || !action) return 0;
    for (size_t i = 0; i < sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]); ++i) {
        if (strcmp(id, DESCRIPTORS[i].id) != 0) continue;
        *action = DESCRIPTORS[i].action;
        return 1;
    }
    return 0;
}

static const CheatDescriptor *descriptor(CheatAction action)
{
    for (size_t i = 0; i < sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]); ++i)
        if (DESCRIPTORS[i].action == action) return &DESCRIPTORS[i];
    return NULL;
}

int cheats_request(CheatAction action)
{
    const CheatDescriptor *command = descriptor(action);
    if (!command) return 0;
    if (action == CHEAT_SECRET_FIGHTERS) {
        ST32(GX_SECRET_FIGHTERS, 1);
    } else {
        ST32(GX_FUNCTION_KEY_STATE, 0);
        if (!function_key_request(command->vk)) return 0;
    }
    activations[action]++;
    return 1;
}

void cheats_tick(void)
{
    function_keys_tick();
}

void cheats_report(void)
{
    for (size_t i = 0; i < sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]); ++i) {
        const CheatDescriptor *entry = &DESCRIPTORS[i];
        if (!activations[entry->action]) continue;
        fprintf(stderr, "cheat: %s %s, %u activation(s), key %s\n", entry->key, entry->label,
                activations[entry->action], !entry->vk || !hostwin_injected_key(entry->vk) ? "released" : "DOWN");
    }
}

/* F3 originally writes state 2 and draws "Function Keys Locked". Preserve its event/replay
 * bit but remove the lock, as this port exposes the actions deliberately. */
void fn_00416e10(void)
{
    const uint32_t state = LD32(R(ESP) + 8);
    if (LD32(state) == 0) {
        const uint32_t event = LD32(R(ESP) + 4);
        ST8(event + 8, LD8(event + 8) | 4u);
        ST32(GX_FUNCTION_KEY_STATE, 0);
    }
    R(ESP) += 4;
}

static int cheat_can_run(void)
{
    return LD32(GX_SCREEN) == 0;
}

static void mark(uint32_t event, uint8_t bit)
{
    ST8(event + 8, LD8(event + 8) | bit);
}

/* These exact cdecl leaf ports deliberately omit only the original non-VS/hidden-code
 * restriction. Their callers clean the arguments; each leaf pops its return address. */
void fn_00416e60(void)
{
    if (cheat_can_run()) {
        mark(LD32(R(ESP) + 4), 0x10);
        ST32(0x0044d034, 1u - LD32(0x0044d034));
        ST32(0x00450c18, LD32(0x00450c18) + 1);
        ST32(GX_FUNCTION_KEY_STATE, 1);
    }
    R(ESP) += 4;
}

void fn_00416eb0(void)
{
    if (cheat_can_run()) {
        mark(LD32(R(ESP) + 4), 0x20);
        if (LD32(GX_COUNTDOWN) == 0) {
            ST32(0x00450bc0, 1u - LD32(0x00450bc0));
            ST32(0x00450c1c, LD32(0x00450c1c) + 1);
            ST32(GX_FUNCTION_KEY_STATE, 1);
        }
    }
    R(ESP) += 4;
}

void fn_00416f10(void)
{
    if (cheat_can_run()) {
        mark(LD32(R(ESP) + 4), 0x40);
        if (LD32(GX_COUNTDOWN) == 0) {
            ST32(0x00450c20, LD32(0x00450c20) + 1);
            ST32(0x00450bb8, 1);
            ST32(GX_FUNCTION_KEY_STATE, 1);
        }
    }
    R(ESP) += 4;
}

void fn_00416f60(void)
{
    if (cheat_can_run()) {
        mark(LD32(R(ESP) + 4), 0x80);
        if (LD32(GX_COUNTDOWN) == 0) {
            ST32(0x00450c24, LD32(0x00450c24) + 1);
            ST32(0x00450bb8, 2);
            ST32(GX_FUNCTION_KEY_STATE, 1);
        }
    }
    R(ESP) += 4;
}
