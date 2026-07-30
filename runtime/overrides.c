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
enum { GX_CLICK = 0x00457580, GX_SCREEN = 0x0044d064 };

/* Selectable items per screen, taken from the game's own hit-test constants -- the centre
 * of each band it brackets the pointer against. Adding a screen is a matter of reading its
 * comparisons out of the disassembly, not of inventing coordinates.
 *
 *   main menu (screen 0):  x 260..547, five entries down the middle
 *   control settings (6):  ok   x 405..560 y 441..465
 *                          cancel x 582..737 y 441..465
 *   recording info (7):    ok   x 231..386 y 416..440
 *                          cancel x 403..558 y 416..440
 *
 * The recording page's "click here to know more" link (x 44..483, y 461..484) is
 * deliberately not selectable: it opens a web page, and a controller should not be able to
 * fall onto something that leaves the game.
 */
typedef struct { int x, y; } Item;

static const Item MAIN_MENU[] = {
    { 403, 228 },  /* game start       */
    { 403, 259 },  /* network game     */
    { 403, 292 },  /* control settings */
    { 403, 322 },  /* recording info   */
    { 403, 353 },  /* official website */
};
static const Item CONTROL_SETTINGS[] = {
    { 482, 453 },  /* ok     */
    { 659, 453 },  /* cancel */
};

static const Item RECORDING_INFO[] = {
    { 308, 428 },  /* ok     */
    { 480, 428 },  /* cancel */
};

/* fn_004246b0 is a __thiscall method and [this+0] is the TOP-level mode: 0 while loading,
 * 1 the front-end menu, 2 character selection (which it dispatches to fn_0041bc90).
 * 0x0044d064 is only the sub-screen within mode 1, so keying off it alone would let these
 * tables fire on the character-select screen whenever that variable happened to hold a
 * matching value. Both are checked. */
enum { MODE_FRONT_END = 1 };

static const struct { uint32_t screen; const Item *items; int n; } SCREENS[] = {
    { 0, MAIN_MENU,        (int)(sizeof MAIN_MENU / sizeof MAIN_MENU[0]) },
    { 6, CONTROL_SETTINGS, (int)(sizeof CONTROL_SETTINGS / sizeof CONTROL_SETTINGS[0]) },
    { 7, RECORDING_INFO,   (int)(sizeof RECORDING_INFO / sizeof RECORDING_INFO[0]) },
};

static int menu_index;
static int menu_confirm_frames;
static int menu_owns_pointer;
static uint32_t menu_wrote_x, menu_wrote_y;
static uint32_t menu_last_screen = 0xffffffffu;

static const Item *screen_items(uint32_t screen, int *n)
{
    for (unsigned i = 0; i < sizeof SCREENS / sizeof SCREENS[0]; i++)
        if (SCREENS[i].screen == screen) { *n = SCREENS[i].n; return SCREENS[i].items; }
    *n = 0;
    return NULL;
}

int menu_move(int delta)
{
    int n = 0;
    if (!screen_items(LD32(GX_SCREEN), &n) || n == 0) return 0;
    menu_index += delta;
    if (menu_index < 0) menu_index = n - 1;
    if (menu_index >= n) menu_index = 0;
    menu_owns_pointer = 1;
    return 1;
}

void menu_confirm(void)
{
    int n = 0;
    if (!screen_items(LD32(GX_SCREEN), &n) || n == 0) return;
    menu_confirm_frames = 2;
}

/* Keep pad and mouse consistent: if the pointer is somewhere the port did not put it, a
 * mouse is in use, so adopt what it points at and hand control back. */
static void menu_sync_from_pointer(const Item *items, int n)
{
    const uint32_t px = LD32(GX_MOUSE_X), py = LD32(GX_MOUSE_Y);
    if (menu_owns_pointer && px == menu_wrote_x && py == menu_wrote_y) return;

    menu_owns_pointer = 0;
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < n; i++) {
        const int dx = (int)px - items[i].x, dy = (int)py - items[i].y;
        const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best >= 0 && best_d <= 90) menu_index = best;
}

void fn_004246b0(void)
{
    const uint32_t mode = R(ECX) ? LD32(R(ECX)) : 0xffffffffu;
    const uint32_t screen = LD32(GX_SCREEN);
    if (getenv("LF2_MENU_DEBUG")) {
        static uint32_t last = 0xfffffffdu;
        if (screen != last) { last = screen; fprintf(stderr, "menu screen = %u\n", screen); }
        static uint32_t last_mode = 0xfffffffdu;
        const uint32_t mode = R(ECX) ? LD32(R(ECX)) : 0xffffffffu;
        if (mode != last_mode) { last_mode = mode; fprintf(stderr, "menu mode [this] = %u\n", mode); }
    }
    int n = 0;
    const Item *items = (mode == MODE_FRONT_END) ? screen_items(screen, &n) : NULL;

    /* Anything outside the front-end menu, or without an item table, is pure delegation. */
    if (!items) { fn_004246b0__orig(); return; }

    if (screen != menu_last_screen) {      /* entering a screen starts at its first item */
        menu_last_screen = screen;
        menu_index = 0;
        menu_owns_pointer = 0;
        menu_confirm_frames = 0;
    }

    menu_sync_from_pointer(items, n);

    if (menu_owns_pointer || menu_confirm_frames) {
        menu_wrote_x = (uint32_t)items[menu_index].x;
        menu_wrote_y = (uint32_t)items[menu_index].y;
        ST32(GX_MOUSE_X, menu_wrote_x);
        ST32(GX_MOUSE_Y, menu_wrote_y);
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
