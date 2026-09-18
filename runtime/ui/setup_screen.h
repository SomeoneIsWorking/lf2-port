#ifndef LF2_SETUP_SCREEN_H
#define LF2_SETUP_SCREEN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SetupScreenResult {
    SETUP_SCREEN_ERROR = -1,
    SETUP_SCREEN_CANCELLED = 0,
    SETUP_SCREEN_RESOLVED = 1,
} SetupScreenResult;

/* Run the first-run setup screen until the player has supplied a location that
 * resolves to a complete Little Fighter 2 v2.0a install, or has given up.
 *
 * The screen is drawn in the port's own window by the shared setup-ui module,
 * so a player never meets a terminal or a system message box. Choosing is the
 * host's (runtime/platform/game_picker); resolving an installer, ZIP,
 * executable, or tree is runtime/app/game_selection's; deciding the resolved
 * tree is really LF2 v2.0a is runtime/app/game_data's. This owns only the
 * screen and the order those run in, and it reports a rejection in the screen
 * rather than tearing it down, so the player can simply choose again.
 *
 * On success `executable` holds the resolved lf2.exe. On any other result
 * `error` says why, ready to show or log. */
SetupScreenResult setup_screen_run(const char *message, char *executable, size_t executable_capacity, char *error,
                                   size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
