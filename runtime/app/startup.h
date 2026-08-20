/* The port's startup policy: initialise the guest, load its data, and make the first
 * presented/interactable screen the post-load mode menu (issue #71).
 *
 * This is deliberately separate from menu input. The retired front end used to be skipped by
 * manufacturing a click on "Game Start", which made boot depend on the very input path the
 * replacement settings UI is meant to own. The world constructor establishes loader mode as
 * its initial state, before the first update can draw or accept input, and the host suppresses
 * presentation until loading has completed. The loader itself still runs unchanged.
 */
#ifndef LF2_STARTUP_H
#define LF2_STARTUP_H

#include <stdint.h>

/* Called around the original top-level update. `self` is the guest object whose first word is
 * the game's top-level mode: 1 loader, 2 game proper on the port's boot route. */
void startup_before_game_frame(uint32_t self, uint32_t mode);
void startup_after_game_frame(uint32_t self, uint32_t mode_before);

/* False while the retired front end and loading picture are running. */
int startup_present_enabled(void);
struct SDL_Window;
void startup_reveal_window(struct SDL_Window *window);

#endif
