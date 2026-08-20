/* Exact guest-side transition performed by the original front end's Game Start branch. */
#ifndef LF2_BOOT_GUEST_H
#define LF2_BOOT_GUEST_H

#include <stdint.h>

void boot_guest_enter_loader(uint32_t game);

#endif
