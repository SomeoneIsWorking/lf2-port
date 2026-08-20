#ifndef LF2_KEYBOARD_H
#define LF2_KEYBOARD_H

#include <SDL3/SDL.h>

/* Record every transition before a modal UI can consume it. Continuous game actions use held
 * state; the global menu consumes Escape's edge once so a down/up pair in one pump is not lost. */
void keyboard_note(unsigned vk, int down);
int keyboard_held(unsigned vk);
int keyboard_take_escape(void);
unsigned keyboard_vk_from_scancode(SDL_Scancode scancode);
SDL_Scancode keyboard_scancode_from_vk(unsigned vk);

#endif
