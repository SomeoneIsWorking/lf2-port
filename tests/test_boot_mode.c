#include "boot_guest.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(boot_guest_target_mode(BOOT_GUEST_LOAD) == BOOT_GUEST_LOAD);
    assert(boot_guest_target_mode(BOOT_GUEST_GAME) == BOOT_GUEST_GAME);
    assert(boot_guest_target_mode(BOOT_GUEST_FRONTEND) == BOOT_GUEST_LOAD);
    assert(boot_guest_target_mode(6) == 6);
    assert(boot_guest_target_mode(0xffffffffu) == 0xffffffffu);
    puts("boot mode: only the retired front end routes through the project loader");
    return 0;
}
