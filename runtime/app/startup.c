#include "startup.h"

#include "boot_guest.h"

#include <stdio.h>

enum { TOP_FRONTEND = 0, TOP_LOAD = 1, TOP_GAME = 2 };
enum { START_INITIALISING, START_LOADING, START_WAITING_FOR_MENU, START_READY };

static int phase;

void startup_before_game_frame(uint32_t self, uint32_t mode)
{
    if (phase == START_INITIALISING && self && mode == TOP_FRONTEND) {
        /* fn_004246b0's front-end branch does not return until a menu choice changes its
         * state. Entering the original body first therefore cannot be followed by a direct
         * transition. Process-wide DirectDraw/audio setup has already completed in its
         * caller, so take the original Game Start transition before entering the body. */
        boot_guest_enter_loader(self);
        phase = START_LOADING;
        fprintf(stderr, "startup: guest initialised; entering its loader directly\n");
        return;
    }

    /* Mode 2 can only follow the mode-1 loader on the boot route. Make that first game-proper
     * frame visible before its body draws, so the first presented picture is the mode menu
     * rather than the loading frame that changed the word to 2. */
    if (phase == START_WAITING_FOR_MENU && mode == TOP_GAME) {
        phase = START_READY;
        fprintf(stderr, "startup: data loaded; presenting the mode menu\n");
    }
}

void startup_after_game_frame(uint32_t self, uint32_t mode_before)
{
    if (!self) return;

    if (phase == START_LOADING && mode_before == TOP_LOAD) {
        /* The original mode-1 branch has completed the real load and stored mode 2. Keep its
         * loading picture hidden; the following mode-2 frame is the first one we present. */
        phase = START_WAITING_FOR_MENU;
    }
}

int startup_present_enabled(void)
{
    return phase == START_READY;
}
