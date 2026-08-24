/* Boot policy at the guest's world-construction boundary. */
#ifndef LF2_BOOT_GUEST_H
#define LF2_BOOT_GUEST_H

#include <stdint.h>

enum { BOOT_GUEST_FRONTEND = 0, BOOT_GUEST_LOAD = 1, BOOT_GUEST_GAME = 2 };

/* Complete the game's one-time data initialisation directly, without entering its retired
 * mode-1 loading-screen branch. `frame_surface` is fn_004246b0's one guest argument. */
void boot_guest_load_data(uint32_t game, uint32_t frame_surface);
int boot_guest_loading_data(void);

#endif
