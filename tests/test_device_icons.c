#include <assert.h>
#include <stdio.h>

#include "device_icons.h"

int main(void)
{
    assert(!device_icon_hud_visible(0, 0));
    assert(device_icon_hud_visible(1, 0));
    assert(!device_icon_hud_visible(1, 1));
    assert(!device_icon_hud_visible(0, 1));

    assert(device_icon_charselect_phase(0, 0) == DEVICE_ICON_CHARSELECT_NONE);
    assert(device_icon_charselect_phase(1, 0) == DEVICE_ICON_CHARSELECT_PRESENT);
    assert(device_icon_charselect_phase(1, 1) == DEVICE_ICON_CHARSELECT_BEFORE_OVERLAY);
    assert(device_icon_charselect_phase(0, 1) == DEVICE_ICON_CHARSELECT_NONE);

    puts("device icons: HUD visibility and character-select painter order");
    return 0;
}
