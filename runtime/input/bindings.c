#include "bindings.h"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

static const char *const ACTION_ID[B_N] = {
    "up", "down", "left", "right", "attack", "jump", "defend",
};
static const char *const KEY_SETTING[B_N] = {
    "key_up", "key_down", "key_left", "key_right",
    "key_attack", "key_jump", "key_defend",
};
static const char *const PAD_SETTING[B_N] = {
    "pad_up", "pad_down", "pad_left", "pad_right",
    "pad_attack", "pad_jump", "pad_defend",
};
static const uint32_t KEY_DEFAULT[B_N] = {
    0x26, 0x28, 0x25, 0x27, 0x5A, 0x58, 0x43,
};
static const SDL_GamepadButton PAD_DEFAULT[B_N] = {
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
};

const char *binding_action_id(int action) { return action >= 0 && action < B_N ? ACTION_ID[action] : ""; }

uint32_t binding_key_vk(int action)
{
    if (action < 0 || action >= B_N) return 0;
    const char *v = config_get(KEY_SETTING[action]);
    if (!v || !*v) return KEY_DEFAULT[action];
    char *end = NULL;
    const long n = strtol(v, &end, 0);
    return end != v && n > 0 && n < 256 ? (uint32_t)n : KEY_DEFAULT[action];
}

void binding_set_key_vk(int action, uint32_t vk)
{
    if (action < 0 || action >= B_N || vk == 0 || vk >= 256) return;
    char v[16]; snprintf(v, sizeof v, "%u", (unsigned)vk);
    config_set(KEY_SETTING[action], v);
}

const char *binding_key_name(uint32_t vk)
{
    static char buf[16];
    if (vk >= 0x41 && vk <= 0x5A) { snprintf(buf, sizeof buf, "%c", (char)vk); return buf; }
    if (vk >= 0x30 && vk <= 0x39) { snprintf(buf, sizeof buf, "%c", (char)vk); return buf; }
    if (vk >= 0x60 && vk <= 0x69) { snprintf(buf, sizeof buf, "NUM%c", (char)(vk - 0x60 + '0')); return buf; }
    switch (vk) {
    case 0x25: return "LEFT"; case 0x26: return "UP"; case 0x27: return "RIGHT"; case 0x28: return "DOWN";
    case 0x0D: return "ENTER"; case 0x20: return "SPACE"; case 0x09: return "TAB"; case 0x08: return "BKSP";
    case 0x10: return "SHIFT"; case 0x11: return "CTRL"; case 0x12: return "ALT"; case 0x1B: return "ESC";
    default: snprintf(buf, sizeof buf, "VK %02X", (unsigned)vk); return buf;
    }
}

SDL_GamepadButton binding_pad_button(int action)
{
    if (action < 0 || action >= B_N) return SDL_GAMEPAD_BUTTON_INVALID;
    const char *v = config_get(PAD_SETTING[action]);
    if (!v || !*v) return PAD_DEFAULT[action];
    char *end = NULL;
    const long n = strtol(v, &end, 0);
    return end != v && n >= 0 && n < SDL_GAMEPAD_BUTTON_COUNT ? (SDL_GamepadButton)n : PAD_DEFAULT[action];
}

void binding_set_pad_button(int action, SDL_GamepadButton button)
{
    if (action < 0 || action >= B_N || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return;
    char v[16]; snprintf(v, sizeof v, "%d", (int)button);
    config_set(PAD_SETTING[action], v);
}

const char *binding_pad_name(SDL_GamepadButton button)
{
    const char *name = SDL_GetGamepadStringForButton(button);
    return name && *name ? name : "UNBOUND";
}
