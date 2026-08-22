#include "startup.h"

#include "boot_guest.h"

#include <SDL3/SDL.h>
#include <stdio.h>

enum { START_INITIALISING, START_LOADING, START_WAITING_FOR_MENU, START_READY };

static int phase;

void startup_before_game_frame(uint32_t self, uint32_t mode)
{
    if (phase == START_INITIALISING && self && mode == BOOT_GUEST_LOAD) {
        /* The world constructor owns the initial mode and constructs this port directly in
         * loader state. Platform setup is complete by the first update, so initialise local
         * player slots here before the real loader body runs. */
        boot_guest_prepare_local_players();
        phase = START_LOADING;
        fprintf(stderr, "startup: world constructed in local-loader state\n");
        return;
    }

    /* Mode 2 can only follow the mode-1 loader on the boot route. Make that first game-proper
     * frame visible before its body draws, so the first presented picture is the mode menu
     * rather than the loading frame that changed the word to 2. */
    if (phase == START_WAITING_FOR_MENU && mode == BOOT_GUEST_GAME) {
        phase = START_READY;
        fprintf(stderr, "startup: data loaded; presenting the mode menu\n");
    }
}

void startup_after_game_frame(uint32_t self, uint32_t mode_before)
{
    if (!self) return;

    if (phase == START_LOADING && mode_before == BOOT_GUEST_LOAD) {
        /* The original mode-1 branch has completed the real load and stored mode 2. Keep its
         * loading picture hidden; the following mode-2 frame is the first one we present. */
        phase = START_WAITING_FOR_MENU;
    }
}

int startup_present_enabled(void)
{
    return phase == START_READY;
}

void startup_reveal_window(SDL_Window *window)
{
    static int revealed;
    if (revealed || !window) return;
    /* The window starts hidden. Its first backing buffer is now the post-load menu, so there
     * is no interval in which the retired front end, loader, or a black flash can be exposed. */
    SDL_ShowWindow(window);
    revealed = 1;
    fprintf(stderr, "startup: first menu frame presented; window revealed\n");
}
