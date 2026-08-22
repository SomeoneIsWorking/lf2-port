#include "ui_rgba.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(ui_rgba_premultiply(0x00010203u) == 0x00000000u);
    assert(ui_rgba_premultiply(0xff123456u) == 0xff123456u);
    assert(ui_rgba_premultiply(0x80804020u) == 0x80402010u);

    assert(ui_rgba_over_xrgb(0x00123456u, 0x00abcdefu) == 0x00abcdefu);
    assert(ui_rgba_over_xrgb(0xff123456u, 0x00abcdefu) == 0x00123456u);
    assert(ui_rgba_over_xrgb(0x80ff0000u, 0x000000ffu) == 0x0080007fu);

    puts("ui rgba: premultiply and opaque source-over rounding");
    return 0;
}
