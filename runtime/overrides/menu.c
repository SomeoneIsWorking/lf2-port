/* The front end: the menu, its mouse and pad, the advertising panel, widescreen.
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

void fn_004246b0__orig(void);
void fn_00423b00__orig(void);

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
 * Calling convention: ONE stack argument, RET 4. The generated body ends in
 * `R(ESP) += 8` -- it pops the return address AND four bytes of argument -- and the
 * decompiler agrees, calling it as FUN_004246b0(DAT_00455608).
 *
 * This said "no arguments, RET c3, so nothing to pop" and was wrong. It cost real work:
 * an experiment that re-invoked the original body in a loop pushed back only the return
 * address, leaked four bytes an iteration, and aborted the game a long way from the
 * cause. A recompiled body's contract is readable straight out of the generated C --
 * `R(ESP) += n` at its return -- so read it rather than recall it. Measured for every
 * override here: fn_00423b00 +4, fn_004246b0 +8, fn_0043f010 +28, fn_00419a60 +16,
 * fn_0043c4a0 +4, fn_00423940 +4.
 * ------------------------------------------------------------------------ */

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

/* 0x0044d070 was used here as an "which screen is up" word, derived from stage-mode .data
 * dumps where it reads -100 while players pick, 0 while the overlay is up and 1 in the
 * match. It is the GAME MODE, not the screen: in VS mode it reads 1 with the overlay open,
 * so the overlay took no mouse input at all there and only stage mode ever worked. It was
 * derived from stage-mode dumps and checked against stage-mode dumps, which is why it
 * looked perfect. The screen is now taken from what the game DRAWS -- see panel_overlay_up()
 * in runtime/ddraw.c -- and this word is deliberately not used. */

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

/* fn_004246b0 is a __thiscall method and [this+0] is the TOP-level mode. Its own dispatch,
 * read out of the disassembly rather than guessed at:
 *
 *   == 1  a one-shot entry step that immediately stores 2 and returns
 *   == 2  hand the frame to fn_0041bc90 -- character selection and the match
 *   else  fall through into the front-end menu body, which is this whole function
 *
 * So the front end is the DEFAULT branch, not a numbered mode; the observed value there is
 * 0. An earlier version of this gate had it as `mode == 1`, which is the one value that is
 * never live for a whole frame -- the port never ran, and the game simply used its original
 * body, which is exactly why nothing looked wrong.
 *
 * 0x0044d064 is only the sub-screen within the front end, so keying off it alone would let
 * these tables fire during character selection whenever it happened to hold a matching
 * value. Both are checked. */
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
    if (best >= 0 && best_d <= 90) { menu_index = best; return; }

    /* The pointer is nowhere near the menu, so it is not what is driving it -- keep the
     * port's own index and go on asserting it. Without this the front end drew NOTHING
     * highlighted until a key was pressed: at boot the pointer sits at the origin, no item
     * is within reach of it, and the port dropped its selection every frame rather than
     * showing where the keyboard and the pad actually were. The first arrow press then
     * moved from an invisible item 0 to item 1, which reads as the highlight starting on
     * the wrong entry. */
    menu_owns_pointer = 1;
}

/* The top-level mode, cached for the input gather's routing: everything before the game
 * proper routes every device to player one, the game proper assigns devices first come
 * first served. Written by the menu override because fn_004246b0 runs every frame and is
 * the function whose `this` holds the mode. */
static uint32_t top_mode = 0xffffffffu;
uint32_t game_top_mode(void) { return top_mode; }

/* The GUEST half of widescreen: a real wider field of view, not a stretched picture.
 *
 * The distinction is the whole feature. Enlarging only the window makes the game scale its
 * 794-wide composition up, which is the same picture with bigger pixels. What is wanted is
 * MORE WORLD, and that needs the surface the game composes into to be wider AND the game's
 * own idea of its viewport to match. The surface is the host half, in runtime/ddraw.c; this
 * is the game's own idea, and both read lf2_wide_width() so they cannot disagree.
 *
 * The game keeps its viewport size in .data rather than only as immediates, which is what
 * makes this possible at all. Three width/height pairs hold 794/550 at runtime, found by
 * scanning a mid-match .data dump for the literal values:
 *
 *   0x0044d014 / 0x0044d018
 *   0x0044d78c / 0x0044d790
 *   0x00453cd4 / 0x00453cd8
 *
 * All three are written every frame, because which one the world draw reads is the question
 * this has never needed to answer -- narrowing comes after there is a reason to.
 *
 * WRITTEN ON CHANGE, IN BOTH DIRECTIONS, because the window can now be dragged narrower as
 * well as wider (issue #20). A word is only overwritten when it holds the game's own 794 or
 * the width this code last put there: anything else is not a viewport width this port
 * understands, and stomping it would be exactly how a side effect gets shipped by accident.
 * Going back to 794 is a restore, not a no-op -- an early return when the window is no
 * longer wide would leave the game believing in the widest it ever was. */
static void wide_apply(void)
{
    const int want = lf2_wide_width() ? lf2_wide_width() : 794;
    static int applied = 794;
    static const uint32_t WIDTHS[] = { 0x0044d014, 0x0044d78c, 0x00453cd4 };
    /* LF2_WIDE_ONLY=<i> writes just one of them, which is how the set gets narrowed:
     * writing all three works but says nothing about which one the drawing reads. */
    static int only = -2;
    if (only == -2) { const char *e = getenv("LF2_WIDE_ONLY"); only = e ? atoi(e) : -1; }
    for (unsigned i = 0; i < sizeof WIDTHS / sizeof WIDTHS[0]; i++) {
        if (only >= 0 && (int)i != only) continue;
        const uint32_t cur = LD32(WIDTHS[i]);
        if (cur == 794u || cur == (uint32_t)applied) ST32(WIDTHS[i], (uint32_t)want);
    }
    applied = want;
}

void fn_004246b0(void)
{
    wide_apply();

    /* The pause menu is built on this: declining to call the original body is what freezes
     * the game, because this is the function the main loop calls to advance and draw
     * everything. The main loop's own present still runs, so the last drawn frame stays on
     * screen with the menu painted over it. */
    pause_tick();
    if (pause_active()) {
        /* The present lives inside the body that is not being called, so it has to be done
         * here or the window simply stops updating -- which is what happened the first time,
         * and it looked like a hang rather than a pause. */
        present_frozen_frame();
        R(ESP) += 8;                               /* same stack contract as the body */
        return;
    }
    const uint32_t mode = R(ECX) ? LD32(R(ECX)) : 0xffffffffu;
    const uint32_t screen = LD32(GX_SCREEN);
    top_mode = mode;
    controls_hint_enable(mode != MODE_IN_GAME);
    if (getenv("LF2_MENU_DEBUG")) {
        static uint32_t last_screen = 0xfffffffdu, last_mode = 0xfffffffdu;
        if (screen != last_screen || mode != last_mode) {
            last_screen = screen; last_mode = mode;
            fprintf(stderr, "menu mode=%u screen=%u updater=%u\n",
                    mode, screen, LD32(0x00458424));
        }
        /* The pre-fight overlay's selection index, located by diffing .data across one
         * d-pad press (tools/diff_data.py) and confirmed against the frame it drew. */
        static uint32_t last_overlay = 0xfffffffdu;
        const uint32_t overlay = LD32(OVERLAY_SEL);
        if (overlay != last_overlay) {
            last_overlay = overlay;
            fprintf(stderr, "overlay selection = %u\n", overlay);
        }
    }
    /* LF2_OVERLAY_FORCE=<n> pins the pre-fight overlay's selection so each item's screen
     * position can be read off a frame dump. Diagnostic scaffolding for building the
     * mouse hit-test table -- the positions have to come from the game, not from me
     * measuring a screenshot by eye. */
    if (getenv("LF2_OVERLAY_FORCE")) {
        const uint32_t want = (uint32_t)atoi(getenv("LF2_OVERLAY_FORCE"));
        if (LD32(OVERLAY_SEL) != want) ST32(OVERLAY_SEL, want);
    }

    /* The update notice in the top-right corner is gone (see fn_0043f010), so its hit box
     * must go with it, or the menu keeps an invisible control that opens sub-screen -3. The
     * game's own test is `mouse.x >= 725 && mouse.y < 18` with no upper bound on x or lower
     * bound on y -- the whole corner. Swallowing the click here rather than letting the
     * original body act on it is the port owning a control it removed; nothing else in the
     * menu is hit-tested in that region. */
    if (LD32(GX_CLICK) && (int32_t)LD32(GX_MOUSE_X) >= NOTICE_X
                       && (int32_t)LD32(GX_MOUSE_Y) < 18)
        ST32(GX_CLICK, 0);

    modemenu_mouse();            /* pointer -> selection on the post-load mode menu */
    overlay_mouse();             /* pointer -> selection on the pre-fight overlay */
    exit_to_menu_tick();         /* the pause menu's way back to the front end */
    /* Not while the overlay is up: it sits ON character selection, so both would take the
     * same pointer and the slot cursor would wander while the player aims at "Fight!". */
    if (!overlay_open()) charselect_mouse();

    int n = 0;
    const int front_end = (mode != MODE_ENTER && mode != MODE_IN_GAME);
    const Item *items = front_end ? screen_items(screen, &n) : NULL;

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
