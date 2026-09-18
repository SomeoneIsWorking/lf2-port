#ifndef LF2_GAME_PICKER_H
#define LF2_GAME_PICKER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Long enough for any host path this port will meet; the same bound the game
 * data layer uses, so a selection never has to be re-sized on the way in. */
enum { GAME_PICKER_PATH_CAPACITY = 4096 };

typedef enum GamePickResult {
    GAME_PICK_ERROR = -1,
    GAME_PICK_CANCELLED = 0,
    GAME_PICK_SELECTED = 1,
} GamePickResult;

/* Ask the host for the one location holding the player's Little Fighter 2
 * files: the original v2.0a installer, a ZIP of an installed tree, an lf2.exe
 * inside one, or the tree itself. Blocking, and it keeps SDL's event queue
 * pumped while the host's chooser is up, so the setup screen underneath stays
 * alive. What a location has to contain is decided elsewhere -- this returns
 * whatever the player pointed at. */
GamePickResult game_picker_choose(const char *message, char *selection, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
