/* Boot policy at the guest's world-construction boundary. */
#ifndef LF2_BOOT_GUEST_H
#define LF2_BOOT_GUEST_H

#include <stdint.h>

enum { BOOT_GUEST_FRONTEND = 0, BOOT_GUEST_LOAD = 1, BOOT_GUEST_GAME = 2 };

/* The project target has no retired front end. Route that exact state through the real
 * loader; leave unknown values visible rather than masking unrelated guest corruption. */
static inline uint32_t boot_guest_target_mode(uint32_t mode)
{ return mode == BOOT_GUEST_FRONTEND ? BOOT_GUEST_LOAD : mode; }

void boot_guest_prepare_local_players(void);

#endif
