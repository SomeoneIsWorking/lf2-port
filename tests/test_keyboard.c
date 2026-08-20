#include "keyboard.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    keyboard_note(0x1b, 1);
    assert(keyboard_held(0x1b));
    assert(keyboard_take_escape());
    assert(!keyboard_take_escape());

    keyboard_note(0x1b, 1);
    assert(!keyboard_take_escape());
    keyboard_note(0x1b, 0);
    assert(!keyboard_held(0x1b));
    keyboard_note(0x1b, 1);
    keyboard_note(0x1b, 0);
    assert(keyboard_take_escape());
    assert(!keyboard_held(0x1b));

    keyboard_note('Z', 1);
    assert(keyboard_held('Z'));
    assert(!keyboard_take_escape());
    keyboard_note('Z', 0);

    const SDL_Scancode scancodes[] = {
        SDL_SCANCODE_A,    SDL_SCANCODE_Z,      SDL_SCANCODE_0,      SDL_SCANCODE_9,
        SDL_SCANCODE_LEFT, SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_KP_9,
    };
    for (unsigned i = 0; i < sizeof(scancodes) / sizeof(scancodes[0]); i++) {
        const unsigned vk = keyboard_vk_from_scancode(scancodes[i]);
        assert(vk != 0);
        assert(keyboard_scancode_from_vk(vk) == scancodes[i]);
    }

    puts("keyboard state: ok");
    return 0;
}
