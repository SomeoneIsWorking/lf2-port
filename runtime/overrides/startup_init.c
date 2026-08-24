#include "startup_init.h"

#include "hostwin.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static Uint64 step_started;

static int trace_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("LF2_STARTUP_TRACE") != NULL;
    return enabled;
}

void startup_init_step_begin(enum StartupInitStep step, const char *name)
{
    if (!trace_enabled()) return;
    step_started = SDL_GetTicks();
    fprintf(stderr, "startup-init: begin step=%02d/%02d name=%s\n", step, STARTUP_INIT_STEP_COUNT - 1, name);
    fflush(stderr);
}

void startup_init_step_done(enum StartupInitStep step, const char *name)
{
    /* On Cocoa, event pumping is a lifecycle requirement even while the game is performing
     * a synchronous load. The custom entry gives each bounded native phase an event boundary
     * without allowing a game update or music-control call to run early. */
    hostwin_pump();
    if (!trace_enabled()) return;
    fprintf(stderr, "startup-init: done step=%02d/%02d name=%s elapsed_ms=%llu\n", step, STARTUP_INIT_STEP_COUNT - 1,
            name, (unsigned long long)(SDL_GetTicks() - step_started));
    fflush(stderr);
}

void startup_init_ready(void)
{
    if (!trace_enabled()) return;
    fprintf(stderr, "startup-init: ready steps=%d\n", STARTUP_INIT_STEP_COUNT - 1);
    fflush(stderr);
}
