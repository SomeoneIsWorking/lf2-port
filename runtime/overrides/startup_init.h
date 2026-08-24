/* Native orchestration for the guest's two one-shot initialization branches. */
#ifndef LF2_STARTUP_INIT_H
#define LF2_STARTUP_INIT_H

#include <stdint.h>

enum StartupInitStep {
    STARTUP_INIT_LOCAL_PLAYERS = 1,
    STARTUP_INIT_FRONTEND_RESOURCES,
    STARTUP_INIT_FRONTEND_CONTROLS,
    STARTUP_INIT_TRANSIENT_SERVICES,
    STARTUP_INIT_SOUND_EFFECTS,
    STARTUP_INIT_OBJECT_REGISTRY,
    STARTUP_INIT_OBJECT_POOL,
    STARTUP_INIT_PLAYER_SLOTS,
    STARTUP_INIT_GAMEPLAY_RESOURCES,
    STARTUP_INIT_STEP_COUNT
};

void startup_init_step_begin(enum StartupInitStep step, const char *name);
void startup_init_step_done(enum StartupInitStep step, const char *name);
void startup_init_ready(void);

void startup_frontend_initialise(void);
void startup_world_initialise(uint32_t world, uint32_t frame_surface);

#endif
