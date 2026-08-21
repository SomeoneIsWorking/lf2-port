#include <assert.h>
#include <stdio.h>

#include "device_icons.h"

int main(void)
{
    assert(!device_icon_hud_visible(0, 0));
    assert(device_icon_hud_visible(1, 0));
    assert(!device_icon_hud_visible(1, 1));
    assert(!device_icon_hud_visible(0, 1));

    puts("device icons: the pre-fight overlay cannot inherit HUD indicators");
    return 0;
}
