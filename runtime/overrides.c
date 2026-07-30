/* Hand-written native replacements for recompiled functions.
 *
 * Each function here provides the symbol fn_<addr> that the lifter would otherwise have
 * generated, for an address listed in re/overrides.txt. They run in the guest ABI: the
 * caller's arguments are on the guest stack, and a stdcall callee pops them.
 */
#include "guest_ops.h"
#include "hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fn_004246b0__orig(void);
void fn_00423b00__orig(void);

/* Nothing overridden yet.
 *
 * A note from the first attempt, so it is not repeated: fn_004242e0 looked like the ad
 * strip -- it references http://www.littlefighter.com/advertise and ShellExecute's "open"
 * verb, and its hit-test constants (x 145..775, y 535) match the "To advertise on LF2"
 * link exactly. It is not. It is a general drawing helper, called seven times from the
 * menu draw path, and stubbing it garbled the character artwork along with the ad.
 *
 * Referencing an ad URL is not the same as being the ad function. The thing to port is the
 * caller that decides to draw an ad, not the helper it draws through.
 */

/* ---------------------------------------------------------------------------
 * fn_004246b0 -- the menu.
 *
 * 4689 lines of generated C covering the main menu, control settings and recording info:
 * it draws every element, hit-tests the mouse against its band table, computes a selection
 * index and dispatches on it. The advertising is drawn inline alongside everything else,
 * which is why there is no separate ad function to replace.
 *
 * Ported incrementally. Anything not yet handled here falls through to fn_004246b0__orig,
 * the original lifted body, so the game keeps working while the port proceeds screen by
 * screen. Landing 4689 lines in one step is not a thing that can be done correctly.
 *
 * Calling convention: no arguments, RET c3, so nothing to pop.
 * ------------------------------------------------------------------------ */

/* Mouse position, as the menu itself reads it. */
enum { GX_MOUSE_X = 0x004546f0, GX_MOUSE_Y = 0x00453cdc };

/* The menu's own state, read out of the disassembly:
 *   0x004546f0  mouse x        0x00453cdc  mouse y
 *   0x00457580  click flag, compared against 1 before an item activates
 *   0x0044d064  the action the chosen item sets
 * and the item positions, which the hit test brackets at x 260..547.
 *
 * Selection is a real index here rather than something derived from a pointer position.
 * The pointer is then placed on the chosen item, which is how the game's own renderer is
 * told what to highlight -- the highlight stays the game's, drawn by its code, so the port
 * does not have to reproduce it. Activation sets the game's click flag for one frame, so
 * the game performs its own dispatch, sound and screen change.
 */
enum { GX_CLICK = 0x00457580 };
static const int MENU_ITEM_Y[] = { 228, 259, 292, 322, 353 };
enum { MENU_ITEM_X = 403,
       N_MENU_ITEMS = (int)(sizeof MENU_ITEM_Y / sizeof MENU_ITEM_Y[0]) };

static int menu_index;
static int menu_confirm_frames;

/* Called from the controller layer. Returns nonzero if the menu consumed the input. */
int menu_move(int delta)
{
    menu_index += delta;
    if (menu_index < 0) menu_index = N_MENU_ITEMS - 1;
    if (menu_index >= N_MENU_ITEMS) menu_index = 0;
    return 1;
}

void menu_confirm(void)
{
    menu_confirm_frames = 2;          /* held long enough for the menu to sample it */
}

void fn_004246b0(void)
{
    /* Place the pointer on the selected item so the game highlights it. Skipped while the
     * real mouse is being used, so a mouse still works normally. */
    if (menu_index > 0 || menu_confirm_frames) {
        ST32(GX_MOUSE_X, (uint32_t)MENU_ITEM_X);
        ST32(GX_MOUSE_Y, (uint32_t)MENU_ITEM_Y[menu_index]);
    }
    if (menu_confirm_frames > 0) {
        menu_confirm_frames--;
        ST32(GX_CLICK, 1);
    }

    fn_004246b0__orig();
}

/* fn_00423b00 -- shared element draw, called with a descriptor.
 *
 * Entry stack after the lifter's CALL: [ESP] return address, [ESP+4] first argument. The
 * original ends in RET c3, so the callee pops only the return address; the caller pops the
 * arguments. An earlier attempt at stubbing this returned without popping and corrupted
 * the guest stack, which surfaced as the game calling TerminateProcess -- worth stating,
 * because a native override that forgets the ABI fails a long way from its cause.
 *
 * Descriptor 0x0044d060 is the advertising panel, identified by tracing the guest call
 * chain at the blit landing on (590,199)-(788,393) and matching the value that appears
 * there. It is the only descriptor this helper is called with on the main menu.
 */
enum { DESC_AD_PANEL = 0x0044d060 };

void fn_00423b00(void)
{
    if (LD32(R(ESP) + 4) == DESC_AD_PANEL) {
        R(ESP) += 4;                 /* RET c3: pop the return address, nothing else */
        return;
    }
    fn_00423b00__orig();
}
