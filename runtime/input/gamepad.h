/* SDL gamepad ownership and the mapped action state exposed to host consumers. */
#ifndef LF2_GAMEPAD_H
#define LF2_GAMEPAD_H

#include <SDL3/SDL.h>

enum { GAMEPAD_MAX_DEVICES = 4 };

void gamepad_handle_event(const SDL_Event *event);
void virtual_pad_tick(long frame);

int gamepad_player_buttons(int index, unsigned char out[7]);
/* Merge mapped player actions from every attached controller for global UI navigation. */
int gamepad_all_player_buttons(unsigned char out[7]);

int gamepad_start_held(void);
int gamepad_start_index(void);
int gamepad_any_connected(void);

#endif
