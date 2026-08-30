/* Physical and synthetic keyboard state shared by the host pump and game input gather. */
#include "keyboard.h"

static unsigned char held[256];
static int escape_pressed;

void keyboard_note(unsigned vk, int down)
{
    if (vk >= 256) return;
    if (down) {
        /* SDL may drain a complete tap before the next guest update. Commands therefore use
         * a latched edge while continuous actions read the held ledger. */
        if (vk == 0x1b && !held[vk]) escape_pressed = 1;
        held[vk] = 1;
    } else {
        held[vk] = 0;
    }
}

int keyboard_held(unsigned vk)
{
    return vk < 256 && held[vk];
}

int keyboard_take_escape(void)
{
    const int pressed = escape_pressed;
    escape_pressed = 0;
    return pressed;
}

unsigned keyboard_vk_from_scancode(SDL_Scancode scancode)
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) return (unsigned)('A' + (scancode - SDL_SCANCODE_A));
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) return (unsigned)('1' + (scancode - SDL_SCANCODE_1));
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
        return 0x61u + (unsigned)(scancode - SDL_SCANCODE_KP_1);
    switch (scancode) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_KP_0: return 0x60;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_RETURN: return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_DELETE: return 0x2E;
    case SDL_SCANCODE_PERIOD: return 0xBE;
    case SDL_SCANCODE_F1:
    case SDL_SCANCODE_F2:
    case SDL_SCANCODE_F3:
    case SDL_SCANCODE_F4:
    case SDL_SCANCODE_F5:
    case SDL_SCANCODE_F6:
    case SDL_SCANCODE_F7:
    case SDL_SCANCODE_F8:
    case SDL_SCANCODE_F9:
    case SDL_SCANCODE_F10:
    case SDL_SCANCODE_F11:
    case SDL_SCANCODE_F12: return 0x70u + (unsigned)(scancode - SDL_SCANCODE_F1);
    default: return 0;
    }
}

SDL_Scancode keyboard_scancode_from_vk(unsigned vk)
{
    if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0') return SDL_SCANCODE_0;
    if (vk >= 0x70 && vk <= 0x7B) return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - 0x70));
    switch (vk) {
    case 0x25: return SDL_SCANCODE_LEFT;
    case 0x26: return SDL_SCANCODE_UP;
    case 0x27: return SDL_SCANCODE_RIGHT;
    case 0x28: return SDL_SCANCODE_DOWN;
    case 0x0D: return SDL_SCANCODE_RETURN;
    case 0x1B: return SDL_SCANCODE_ESCAPE;
    case 0x20: return SDL_SCANCODE_SPACE;
    case 0x10: return SDL_SCANCODE_LSHIFT;
    case 0x11: return SDL_SCANCODE_LCTRL;
    case 0x12: return SDL_SCANCODE_LALT;
    case 0x09: return SDL_SCANCODE_TAB;
    case 0x2E: return SDL_SCANCODE_DELETE;
    case 0xBE: return SDL_SCANCODE_PERIOD;
    case 0x60: return SDL_SCANCODE_KP_0;
    case 0x61: return SDL_SCANCODE_KP_1;
    case 0x62: return SDL_SCANCODE_KP_2;
    case 0x63: return SDL_SCANCODE_KP_3;
    case 0x64: return SDL_SCANCODE_KP_4;
    case 0x65: return SDL_SCANCODE_KP_5;
    case 0x66: return SDL_SCANCODE_KP_6;
    case 0x67: return SDL_SCANCODE_KP_7;
    case 0x68: return SDL_SCANCODE_KP_8;
    case 0x69: return SDL_SCANCODE_KP_9;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
