#ifndef LF2_GUEST_CURSOR_H
#define LF2_GUEST_CURSOR_H

#include <stdint.h>

/* The three calls in the original binary that draw LF2's sprite cursor. The sheet is shared
 * with menu artwork, so the producer call site is the stable identity; pointer coordinates
 * are layout and change independently under widescreen transforms. */
static inline int guest_cursor_draw(uint32_t return_address, uint32_t sheet, uint32_t cursor_sheet)
{
    if (!sheet || sheet != cursor_sheet) return 0;
    return return_address == 0x00424660u || return_address == 0x00428778u || return_address == 0x004329eau;
}

#endif
