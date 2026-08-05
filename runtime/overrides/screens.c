/* The post-load screens' mouse: mode menu, character selection, the overlay.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"

#include "../guest_ops.h"
#include "../guest_map.h"
#include "../hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Character selection: make the mouse work, alongside the keyboard and pads.
 *
 * The front-end menu is mouse-native and the port adds keyboard/pad to it by moving an
 * index and writing the POINTER onto the chosen item, letting the game's own highlight and
 * dispatch run. Everything after loading is the opposite problem: fn_0041bc90's screens
 * are keyboard-native and hit-test nothing, so here the port goes the other way, pointer
 * -> index, and again lets the game draw and act on it.
 *
 * The state, located rather than guessed:
 *   .data 0x00458c94  an array of object pointers, stride 0x420
 *   OBJ[e] + 0x364    that object's character-select slot cursor
 *
 * Found by diffing the guest HEAP across one right-arrow press with a no-press control:
 * exactly one dword of 25,071,560 changed in the test and none in the control. Confirmed
 * as a cursor rather than a coincidence by stepping it right/right/left and watching
 * 0 -> 2 -> 1. It is NOT in .data, which is why diffing .data found only free-running
 * counters -- see docs/issues for that dead end.
 *
 * The slot rectangles come from the game's own frame, not from measuring a screenshot:
 * the portrait panels are near-black, so scanning a dumped frame for dark runs gives the
 * columns and rows below exactly.
 *
 * Safety, since this writes into a live game object: the write happens only while the
 * top-level mode is the game proper, only when the pointer actually MOVED this frame (so
 * it never fights the keyboard or pad), only when the pointer is inside a slot, and only
 * when the cursor already holds a slot index. A screen id that reliably separates
 * character select from the match has not been found yet -- two candidates next to the
 * known screen variables both turned out to track route progress instead -- so the gate is
 * built to be harmless without one rather than to depend on a guess.
 * ------------------------------------------------------------------------ */
/* OBJ_TABLE is an array of pointers; the objects they point at are 0x420 apart, which is
 * how the table was recognised as a table. Only the pointers are indexed here. */
enum { OBJ_TABLE = 0x00458c94, OBJ_SEL = 0x364, CS_SLOTS = 8 };

/* x runs and y runs of the eight portrait panels, read off a frame dump. */
static const struct { int x0, x1; } CS_COL[4] = {
    { 147, 266 }, { 300, 419 }, { 453, 572 }, { 606, 725 },
};
static const struct { int y0, y1; } CS_ROW[2] = {
    {  95, 282 },      /* portrait 95..213 plus its Player/Fighter/Team rows */
    { 306, 495 },
};

static int cs_slot_at(int x, int y)
{
    for (int r = 0; r < 2; r++) {
        if (y < CS_ROW[r].y0 || y > CS_ROW[r].y1) continue;
        for (int c = 0; c < 4; c++)
            if (x >= CS_COL[c].x0 && x <= CS_COL[c].x1) return r * 4 + c;
    }
    return -1;
}

/* The keyboard is device 0; whichever player it claimed is the one the mouse should also
 * drive, so the two never disagree about who they are. The observed pairing is entry
 * 1 + player, which is what a right-arrow press moved. */
void charselect_mouse(void)
{
    static int dbg0 = -1;
    if (dbg0 < 0) dbg0 = getenv("LF2_CS_DEBUG") != NULL;
    if (dbg0) {
        static long n; int px=-1, py=-1; const int have = hostwin_pointer(&px, &py);
        if (++n % 120 == 0)
            fprintf(stderr, "cs-gate: top_mode=%u have_ptr=%d ptr=(%d,%d)\n",
                    game_top_mode(), have, px, py);
    }
    if (game_top_mode() != MODE_IN_GAME) return;

    /* The port's own pointer, not the game's stale copy -- see hostwin_pointer(). */
    int mx, my;
    if (!hostwin_pointer(&mx, &my)) return;
    static int last_x = -1, last_y = -1;
    const int moved = (mx != last_x || my != last_y);
    last_x = mx; last_y = my;

    const int slot = cs_slot_at(mx, my);
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("LF2_CS_DEBUG") != NULL;
    if (dbg) {
        const int kp0 = keyboard_player();
        const uint32_t e0 = (uint32_t)(1 + (kp0 >= 0 ? kp0 : 0));
        const uint32_t p0 = LD32(OBJ_TABLE + e0 * 4);
        fprintf(stderr, "cs: ptr=(%d,%d) slot=%d kbplayer=%d entry=%u obj=%08x cur=%u\n",
                mx, my, slot, kp0, e0, p0, p0 ? LD32(p0 + OBJ_SEL) : 0xffffffffu);
    }
    if (slot < 0) return;

    const int kp = keyboard_player();
    const uint32_t e = (uint32_t)(1 + (kp >= 0 ? kp : 0));
    const uint32_t objp = LD32(OBJ_TABLE + e * 4);   /* a table of POINTERS, 4 bytes each */
    if (!objp) return;

    const uint32_t cur = LD32(objp + OBJ_SEL);
    if (cur >= CS_SLOTS) {
        /* No slot cursor yet: this player has not joined. A click inside a portrait is
         * the attack that joins -- gated on the phase word so it cannot fire during a
         * match, where the same rectangles are just part of the arena. */
        if (panel_charselect_up() && !panel_overlay_up() && hostwin_mouse_clicked())
            mouse_confirm_frames = 2;
        return;
    }

    /* Hover only when the pointer actually moved, so an idle mouse resting over a slot
     * never fights the keyboard or a pad. A click is an explicit act, so it is honoured
     * whether or not the pointer moved -- clicking twice in the same spot is how a player
     * joins and then confirms, and requiring motion between the two would drop the second. */
    if (moved && (uint32_t)slot != cur) ST32(objp + OBJ_SEL, (uint32_t)slot);

    if (hostwin_mouse_clicked()) {
        ST32(objp + OBJ_SEL, (uint32_t)slot);
        mouse_confirm_frames = 2;
    }
}

/* ---------------------------------------------------------------------------
 * The post-load mode menu -- VS mode / Stage mode / the two championships / Battle mode /
 * Demo / Playback Recording / Quit. This is the screen the front-end launcher hands over
 * to, and the one that makes the two menus feel like different programs: the launcher is
 * mouse-only and this is keyboard-only. Same treatment as character selection, pointer to
 * index, so all three input devices drive it.
 *
 *   .data 0x00451160   the selection, 0..7
 *
 * Located by diffing .data across one down-press against a no-press control -- one dword
 * differed in the test and not in the control -- and confirmed by stepping down/down/up
 * and watching 0 -> 2 -> 1. (The heap diff was empty for this screen; unlike character
 * selection, this selection really is in .data.)
 *
 * The item rows come from the game's own frame: the eight labels sit at ~27.3 px spacing
 * from y 215, in the x band the labels occupy. Measured by scanning a dumped frame for
 * ink rows rather than by reading coordinates off a screenshot.
 * ------------------------------------------------------------------------ */
enum { MODEMENU_SEL = 0x00451160, MODEMENU_ITEMS = 8 };
enum { MM_X0 = 250, MM_X1 = 560, MM_Y0 = 202, MM_STEP_Q = 273 };   /* step 27.3 px, x10 */

static int modemenu_item_at(int x, int y)
{
    if (x < MM_X0 || x > MM_X1) return -1;
    const int rel = (y - MM_Y0) * 10;
    if (rel < 0) return -1;
    const int i = rel / MM_STEP_Q;
    return (i >= 0 && i < MODEMENU_ITEMS) ? i : -1;
}

void modemenu_mouse(void)
{
    if (game_top_mode() != MODE_IN_GAME) return;

    int mx, my;
    if (!hostwin_pointer(&mx, &my)) return;

    const uint32_t cur = LD32(MODEMENU_SEL);
    if (cur >= MODEMENU_ITEMS) return;      /* not holding a menu index: not this screen */

    static int last_x = -1, last_y = -1;
    const int moved = (mx != last_x || my != last_y);
    last_x = mx; last_y = my;

    const int item = modemenu_item_at(mx, my);
    if (item < 0) return;

    /* Hover selects, but only when the pointer actually moved, so an idle mouse resting
     * over an item never fights the keyboard or a pad. */
    if (moved && (uint32_t)item != cur) ST32(MODEMENU_SEL, (uint32_t)item);

    /* A click activates. The game acts on its own attack button, so the click is turned
     * into one, which keeps the dispatch, the sound and the screen change the game's. */
    if (hostwin_mouse_clicked()) {
        ST32(MODEMENU_SEL, (uint32_t)item);
        mouse_confirm_frames = 2;
    }
}

/* ---------------------------------------------------------------------------
 * The pre-fight overlay -- Fight! / Reset All / Reset Random / Background (Stage in stage
 * mode) / Difficulty / Exit. The last screen in the chain that was keyboard-only, so this
 * is what finishes "every menu takes every device".
 *
 *   .data 0x0044d06c   the selection, 0..5 (OVERLAY_SEL, located earlier)
 *
 * Knowing WHEN it is up matters more than it looks. Character selection is still underneath
 * it and charselect_mouse() is still live, so without that the pointer would drag the slot
 * cursor around while the player is aiming at "Fight!". The answer comes from what the game
 * draws (panel_overlay_up()), not from a .data flag -- the first attempt used one, and it
 * was the game mode wearing a convincing disguise.
 *
 * The row geometry comes from the game's own highlight blit, not from measuring a
 * screenshot: LF2_OVERLAY_FORCE pins the selection and LF2_BLT_FRAME prints where the
 * highlight lands. Item 0 -> y 16, item 2 -> y 64, item 5 -> y 137, i.e. 24 per row from
 * 16, with the last row a pixel low in the sheet. The panel is blitted at (3,3)-(307,169),
 * which is the x band.
 * ------------------------------------------------------------------------ */
enum { OVERLAY_ITEMS = 6 };
enum { OV_X0 = 3, OV_X1 = 307, OV_Y0 = 16, OV_STEP = 24 };

int overlay_open(void)
{
    return game_top_mode() == MODE_IN_GAME && panel_overlay_up()
        && LD32(OVERLAY_SEL) < OVERLAY_ITEMS;
}

static int overlay_item_at(int x, int y)
{
    if (x < OV_X0 || x > OV_X1) return -1;
    const int rel = y - OV_Y0;
    if (rel < 0) return -1;
    const int i = rel / OV_STEP;
    return i < OVERLAY_ITEMS ? i : -1;
}

void overlay_mouse(void)
{
    if (!overlay_open()) return;

    int mx, my;
    if (!hostwin_pointer(&mx, &my)) return;

    static int last_x = -1, last_y = -1;
    const int moved = (mx != last_x || my != last_y);
    last_x = mx; last_y = my;

    const int item = overlay_item_at(mx, my);
    if (item < 0) return;

    const uint32_t cur = LD32(OVERLAY_SEL);
    if (moved && (uint32_t)item != cur) ST32(OVERLAY_SEL, (uint32_t)item);

    /* Same contract as the mode menu: the click only places the selection, and the game's
     * own attack button does the acting, so the dispatch, the sound and the screen change
     * stay the game's. */
    if (hostwin_mouse_clicked()) {
        ST32(OVERLAY_SEL, (uint32_t)item);
        mouse_confirm_frames = 2;
        if (getenv("LF2_MENU_DEBUG"))
            fprintf(stderr, "overlay click on item %d at (%d,%d)\n", item, mx, my);
    }
}
