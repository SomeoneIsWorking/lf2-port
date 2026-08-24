#include "boot_guest.h"

#include "guest_ops.h"

void fn_00419e40__orig(void);
void fn_0041bc90(void);
void fn_004028a0(void);
void fn_00422ac0(void);
void fn_00423910(void);
void fn_004246b0__orig(void);
void fn_0043e940__orig(void);

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
    fn_00422ac0();

    ST32(PLAYER_DEVICE_1, 1);
    ST32(PLAYER_DEVICE_2, 2);
    ST32(PLAYER_DEVICE_3, 3);
    ST32(PLAYER_DEVICE_4, 4);
}

static int loading_data;

int boot_guest_loading_data(void) { return loading_data; }

static void initialise_frontend_resources(uint32_t game, uint32_t frame_surface)
{
    /* The game's one-time keyboard tables and menu resources are in the DAT_0044d068
     * initializer at the head of fn_004246b0's mode-0 branch; they are not part of the
     * loading screen or the data loader. Run that initializer synchronously before loading.
     * Drawing and presentation are declined while loading_data is set, so no retired screen
     * participates in startup. Keeping the shadow body as the authority avoids duplicating
     * its keyboard mappings and resource constructors in a native table. The shadow body is
     * RET 4 and consumes both words below.
     *
     * The former delayed-reveal path appeared to skip mode 0, but its startup hook left ECX
     * clobbered by fn_00422ac0 before calling the shadow body. The resulting wrong `this`
     * accidentally entered this initializer. Calling it deliberately is required; omitting
     * it leaves the character-select keyboard tables zeroed. */
    ST32(game, BOOT_GUEST_FRONTEND);
    R(ECX) = game;
    PUSH32(frame_surface);
    PUSH32(0x004246b0);
    fn_004246b0__orig();
}

void fn_00419e40(void)
{
    /* fn_00419e40 is the world constructor and the sole owner of its initial top-level mode.
     * Keep the game's complete constructor, then choose the port's actual initial state here:
     * local loader, not the retired front end. This is earlier than the first update and does
     * not reproduce a menu selection, click flag, sound stop, or key/button event. */
    const uint32_t game = R(ECX);
    fn_00419e40__orig();
    ST8(LOCAL_PLAY_MARKER, 'u');
    ST32(game, BOOT_GUEST_LOAD);
}

void boot_guest_load_data(uint32_t game, uint32_t frame_surface)
{
    /* The first top-level update happens after platform setup, which is the earliest point at
     * which the game's data initialiser can create its DirectDraw and audio resources. Prepare
     * the local slots it expects, then call mode 2 directly: its one-time DAT_0044d05c branch
     * loads the data synchronously and clears that latch before returning.
     *
     * Do not call fn_004246b0__orig's mode-1 branch. That branch exists to clean up and draw
     * the retired loading screen before selecting mode 2; using it and hiding the SDL window
     * does not bypass anything. fn_0041bc90 is __thiscall with one stack argument and RET 4,
     * so its generated body consumes both words pushed below. */
    initialise_local_players();
    loading_data = 1;
    initialise_frontend_resources(game, frame_surface);

    /* Preserve the mode-1 branch's non-presentation setup. The first call releases its
     * transient loading-art surface when present; the second advances the game's frame/audio
     * services once after selecting mode 2. Both are distinct from drawing/flipping the
     * loading picture. fn_004028a0 is cdecl, so its caller removes the argument. */
    PUSH32(0x00424708);
    fn_00423910();
    ST32(game, BOOT_GUEST_GAME);
    PUSH32(frame_surface);
    PUSH32(0x00424728);
    fn_004028a0();
    R(ESP) += 4;

    R(ECX) = game;
    PUSH32(frame_surface);
    PUSH32(0x00424746);
    fn_0041bc90();
    ST32(CLICK_FLAG, 0);
    loading_data = 0;
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
    fn_0043e940__orig();
}
