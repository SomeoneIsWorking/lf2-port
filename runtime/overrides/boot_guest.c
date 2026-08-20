#include "boot_guest.h"

#include "guest_ops.h"

void fn_00401a30(void);
void fn_00422ac0(void);

enum {
    CLICK_FLAG = 0x00457580,
    FRONTEND_SOUND = 0x00455610,
    LOCAL_PLAY_MARKER = 0x004553bf,
    FRONTEND_SUBSTATE = 0x0044d064,
    PLAYER_DEVICE_1 = 0x00450b4c,
    PLAYER_DEVICE_2 = 0x00450b50,
    PLAYER_DEVICE_3 = 0x00450b54,
    PLAYER_DEVICE_4 = 0x00450b58,
    TOP_LOAD = 1
};

static void stop_frontend_sound(void)
{
    /* fn_00401a30 is __thiscall with one stack argument and RET 4. These are the ECX and
     * argument used by the original Game Start branch at 0x00427a0c. */
    PUSH32(0);
    R(ECX) = FRONTEND_SOUND;
    PUSH32(0x00427a19);
    fn_00401a30();
}

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

void boot_guest_enter_loader(uint32_t game)
{
    /* This is the non-rendering part of the original Game Start branch, read directly from
     * 0x00427a0c..0x00427a60. Calling it is a state transition, not manufactured input. */
    ST32(CLICK_FLAG, 0);
    stop_frontend_sound();
    ST8(LOCAL_PLAY_MARKER, 'u');
    ST32(FRONTEND_SUBSTATE, 0);
    ST32(game, TOP_LOAD);
    initialise_local_players();
}
