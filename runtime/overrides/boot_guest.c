#include "boot_guest.h"
#include "startup_init.h"

#include "dsound.h"
#include "guest.h"
#include "jit_executor.h"

enum {
    CLICK_FLAG = 0x00457580,
    LOCAL_PLAY_MARKER = 0x004553bf,
    PLAYER_DEVICE_1 = 0x00450b4c,
    PLAYER_DEVICE_2 = 0x00450b50,
    PLAYER_DEVICE_3 = 0x00450b54,
    PLAYER_DEVICE_4 = 0x00450b58,
};

static void initialise_local_players(void)
{
    /* No arguments; the function consumes only the synthetic return address. */
    PUSH32(0x00427a31);
    lf2_jit_call(0x00422ac0);

    ST32(PLAYER_DEVICE_1, 1);
    ST32(PLAYER_DEVICE_2, 2);
    ST32(PLAYER_DEVICE_3, 3);
    ST32(PLAYER_DEVICE_4, 4);
}

static int loading_data;

int boot_guest_loading_data(void)
{
    return loading_data;
}

void fn_00419e40(void)
{
    /* fn_00419e40 is the world constructor and the sole owner of its initial top-level mode.
     * Keep the game's complete constructor, then choose the port's actual initial state here:
     * local loader, not the retired front end. This is earlier than the first update and does
     * not reproduce a menu selection, click flag, sound stop, or key/button event. */
    const uint32_t game = R(ECX);
    lf2_jit_call_original(0x00419e40);
    ST8(LOCAL_PLAY_MARKER, 'u');
    ST32(game, BOOT_GUEST_LOAD);
}

void boot_guest_load_data(uint32_t game, uint32_t frame_surface)
{
    /* The guest put both one-time constructors inside two ordinary frame functions. Calling
     * those monoliths during startup also runs launcher/ad update work or the first game-mode
     * update, uses their large MSVC SEH frames, and leaves Cocoa without an event boundary for
     * the entire load. The native initializers below are direct ports of only the guarded
     * DAT_0044d068 and DAT_0044d05c branches. They preserve guest allocations, constructors,
     * constants and ABI, stop at each latch clear, and leave normal updating to the next frame. */
    startup_init_step_begin(STARTUP_INIT_LOCAL_PLAYERS, "local-players");
    audio_initialization_begin();
    initialise_local_players();
    startup_init_step_done(STARTUP_INIT_LOCAL_PLAYERS, "local-players");

    loading_data = 1;
    startup_frontend_initialise();

    startup_init_step_begin(STARTUP_INIT_TRANSIENT_SERVICES, "transient-services");
    PUSH32(0x00424708);
    lf2_jit_call(0x00423910);
    ST32(game, BOOT_GUEST_GAME);
    PUSH32(frame_surface);
    PUSH32(0x00424728);
    lf2_jit_call(0x004028a0);
    R(ESP) += 4;
    startup_init_step_done(STARTUP_INIT_TRANSIENT_SERVICES, "transient-services");

    startup_world_initialise(game, frame_surface);
    ST32(CLICK_FLAG, 0);
    loading_data = 0;
    audio_initialization_end();
}

void fn_0043e940(void)
{
    /* Every loading/progress presenter below fn_0041bc90 reaches this one guest boundary.
     * The startup load is a blocking data operation, not a screen: decline those presents at
     * their game-owned function instead of hiding the SDL window or discarding host frames.
     * fn_0043e940 is cdecl: it pops only its return address; each caller removes the one
     * argument. */
    if (loading_data) {
        R(ESP) += 4;
        return;
    }
    lf2_jit_call_original(0x0043e940);
}
