/* The port's startup policy: initialise the guest, load its data, and make the first
 * interactable screen the post-load mode menu (issues #71 and #102).
 *
 * This is deliberately separate from menu input. The retired front end used to be skipped by
 * manufacturing a click on "Game Start", which made boot depend on the very input path the
 * replacement settings UI is meant to own. The world constructor establishes loader mode as
 * its initial state, then the first update calls the real data initialiser directly instead of
 * entering the loading-screen branch. The SDL window stays visible throughout.
 */
#ifndef LF2_STARTUP_H
#define LF2_STARTUP_H

#include <stdint.h>

/* Run the one-time synchronous load when the constructed world is in mode 1. Returns true
 * when it consumed fn_004246b0's update and the caller must perform that function's RET 4. */
int startup_load_data(uint32_t self, uint32_t mode, uint32_t frame_surface);

#endif
