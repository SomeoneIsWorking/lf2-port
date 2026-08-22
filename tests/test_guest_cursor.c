#include "guest_cursor.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    const unsigned sheet = 0x200940f0u;
    assert(guest_cursor_draw(0x00424660u, sheet, sheet));
    assert(guest_cursor_draw(0x00428778u, sheet, sheet));
    assert(guest_cursor_draw(0x004329eau, sheet, sheet));
    assert(!guest_cursor_draw(0x00428778u, sheet + 4, sheet));
    assert(!guest_cursor_draw(0x00428779u, sheet, sheet));
    assert(!guest_cursor_draw(0x00428778u, 0, 0));
    puts("guest cursor: only the three cursor producers are declined");
    return 0;
}
