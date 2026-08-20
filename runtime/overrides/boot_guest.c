#include "boot_guest.h"

#include "guest_ops.h"

void fn_00419e40__orig(void);
void fn_00422ac0(void);

enum {
    LOCAL_PLAY_MARKER = 0x004553bf,
    PLAYER_DEVICE_1 = 0x00450b4c,
    PLAYER_DEVICE_2 = 0x00450b50,
    PLAYER_DEVICE_3 = 0x00450b54,
    PLAYER_DEVICE_4 = 0x00450b58,
    TOP_LOAD = 1
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

void fn_00419e40(void)
{
    /* fn_00419e40 is the world constructor and the sole owner of its initial top-level mode.
     * Keep the game's complete constructor, then choose the port's actual initial state here:
     * local loader, not the retired front end. This is earlier than the first update and does
     * not reproduce a menu selection, click flag, sound stop, or key/button event. */
    const uint32_t game = R(ECX);
    fn_00419e40__orig();
    ST8(LOCAL_PLAY_MARKER, 'u');
    ST32(game, TOP_LOAD);
}

void boot_guest_prepare_local_players(void)
{
    /* The loader reaches its first update only after platform setup. Initialise the same local
     * player slots it expects there, without involving any front-end selection state. */
    initialise_local_players();
}
