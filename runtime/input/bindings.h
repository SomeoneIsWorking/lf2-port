/* Persistent player-action bindings, independent of the UI that edits them (issue #70). */
#ifndef LF2_BINDINGS_H
#define LF2_BINDINGS_H

#include <SDL3/SDL.h>
#include <stdint.h>

enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_ATTACK, B_JUMP, B_DEFEND, B_N };

const char *binding_action_id(int action);
uint32_t binding_key_vk(int action);
void binding_set_key_vk(int action, uint32_t vk);
const char *binding_key_name(uint32_t vk);
SDL_GamepadButton binding_pad_button(int action);
void binding_set_pad_button(int action, SDL_GamepadButton button);
const char *binding_pad_name(SDL_GamepadButton button);

#endif
