/* The front end: the menu, its mouse and pad, the advertising panel, widescreen.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "boot_guest.h"
#include "world.h"

#include "guest_ops.h"
#include "guest_map.h"
#include "hostwin.h"
#include "startup.h"

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

/* The post-load mode menu's selection -- screens.c located it and drives it for the mouse. */
enum { MODEMENU_SEL_W = 0x00451160, MODEMENU_ITEMS_W = 8 };

static const char *const MODE_NAME[MODEMENU_ITEMS_W] = {
    "vs", "stage", "championship1", "championship2", "battle", "demo", "playback", "quit"
};

static long mode_force_frames;
static int  mode_force_want = -2;      /* -2 not looked, -1 no request or a bad one */
static int  mode_force_done;

static void mode_force_tick(void)
{
    if (mode_force_want == -2) {
        const char *v = getenv("LF2_MODE");
        mode_force_want = -1;
        if (v) {
            for (int i = 0; i < MODEMENU_ITEMS_W; i++)
                if (strcmp(v, MODE_NAME[i]) == 0) { mode_force_want = i; break; }
            if (mode_force_want < 0)
                fprintf(stderr, "menu: LF2_MODE=%s names no mode -- the run will enter "
                                "whatever the menu was already on. Try one of: vs stage "
                                "championship1 championship2 battle demo playback quit\n", v);
            else
                fprintf(stderr, "menu: LF2_MODE=%s -- holding the mode menu on item %d\n",
                        v, mode_force_want);
        }
    }
    if (mode_force_want < 0 || mode_force_done) return;
    /* Once the overlay is up the mode has been chosen and the screen is gone. */
    if (panel_overlay_up()) {
        mode_force_done = 1;
        fprintf(stderr, "menu: LF2_MODE held the mode menu for %ld frame(s); the overlay is up, "
                        "so the mode is chosen and the hold is released\n", mode_force_frames);
        return;
    }
    if (LD32(MODEMENU_SEL_W) >= MODEMENU_ITEMS_W) return;   /* not that screen */
    if (LD32(MODEMENU_SEL_W) != (uint32_t)mode_force_want)
        ST32(MODEMENU_SEL_W, (uint32_t)mode_force_want);
    mode_force_frames++;
}

/* Said at exit, because "the mode was never held" and "the mode was held" look identical in a
 * frame dump, and a route that silently entered VS when it asked for stage would be a green
 * test for a mode it never visited. */
void mode_force_report(void)
{
    if (mode_force_want < 0) return;
    if (mode_force_frames)
        fprintf(stderr, "menu: LF2_MODE=%s was held on %ld frame(s)%s\n",
                MODE_NAME[mode_force_want], mode_force_frames,
                mode_force_done ? " and released at the overlay" : " and never released -- the "
                                  "run did not reach the pre-fight overlay");
    else
        fprintf(stderr, "menu: LF2_MODE=%s was NEVER held -- the mode menu was not reached in "
                        "this run, so the game entered whatever it was already on and this "
                        "run says NOTHING about %s mode\n",
                MODE_NAME[mode_force_want], MODE_NAME[mode_force_want]);
}

/* 0x0044d070 was used here as an "which screen is up" word, derived from stage-mode .data
 * dumps where it reads -100 while players pick, 0 while the overlay is up and 1 in the
 * match. It is the GAME MODE, not the screen: in VS mode it reads 1 with the overlay open,
 * so the overlay took no mouse input at all there and only stage mode ever worked. It was
 * derived from stage-mode dumps and checked against stage-mode dumps, which is why it
 * looked perfect. The screen is now taken from what the game DRAWS -- see panel_overlay_up()
 * in runtime/video/ddraw.c -- and this word is deliberately not used. */

/* Issue #27's instrument: the game's click flag as the front-end MENU sees it, with the
 * count printed at exit whether or not it is zero.
 *
 * The watch on 0x00457580 cannot answer this -- it reports per host call, so it says the
 * flag changed between two calls, not whether the menu ever saw it set. And "no frame
 * reached the menu with the flag set" is the interesting answer, so it has to be printed:
 * a probe that only speaks when it finds something cannot be told apart from one that never
 * looked. That negative is exactly what identified the pad path as not using the flag at
 * all. Under LF2_MENU_DEBUG, where this screen's other diagnostics already live. */
static long menu_click_seen;

static int menu_click_debug(void)
{
    static int on = -1;
    if (on < 0) on = getenv("LF2_MENU_DEBUG") != NULL;
    return on;
}

void menu_click_report(void)
{
    if (!menu_click_debug()) return;
    fprintf(stderr, "menu-click: %ld frame(s) reached the front-end menu with the game's "
                    "click flag set%s\n", menu_click_seen,
            menu_click_seen ? "" : " -- so nothing the menu could act on ever arrived");
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
 * own idea of its viewport to match. The surface is the host half, in runtime/video/ddraw.c; this
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

    /* RmlUi is composited by the ordinary present inside the original body. Keeping that body
     * as the single update/draw owner avoids a separate frozen-frame transition; the active
     * document remains modal because the host input boundary withholds input from LF2. */
    pause_tick();
    const uint32_t self = R(ECX);
    uint32_t mode = self ? LD32(self) : 0xffffffffu;
    const uint32_t target_mode = boot_guest_target_mode(mode);
    if (self && target_mode != mode) {
        ST32(self, target_mode);
        mode = target_mode;
        fprintf(stderr, "startup: retired front-end state was discarded; entering the "
                        "project loader\n");
    }
    startup_before_game_frame(self, mode);
    mode = self ? LD32(self) : 0xffffffffu;
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
         * d-pad press (tools/re/diff_data.py) and confirmed against the frame it drew. */
        static uint32_t last_overlay = 0xfffffffdu;
        const uint32_t overlay = LD32(OVERLAY_SEL);
        if (overlay != last_overlay) {
            last_overlay = overlay;
            fprintf(stderr, "overlay selection = %u\n", overlay);
        }
    }
    /* ---- LF2_MODE=<name>: which game mode a scripted run enters ----
     *
     * THE PORT OWNS THE PATH IN. Before this, every route reached the game by pressing
     * buttons at counted frames and taking whatever the mode menu happened to be sitting on,
     * which is VS mode -- so the whole test suite only ever exercised one of the eight modes,
     * and stage-mode work (issue #36) had no way to be verified at all.
     *
     * This does not press anything. It puts the GAME'S OWN selection where the run asked for
     * it and lets the route's existing confirm dispatch it, so the mode change, its sound and
     * its screen transition are all the game's -- the same shape as the mouse hover in
     * screens.c, which writes the same word.
     *
     * The eight items are the game's own order (screens.c): VS, Stage, the two championships,
     * Battle, Demo, Playback, Quit. Named rather than numbered because a route reading
     * `LF2_MODE=stage` says what it is doing and `LF2_MODE=1` does not, and an unknown name is
     * REFUSED loudly rather than silently entering VS -- a run that quietly took the wrong
     * mode would produce a green test for a mode it never visited.
     *
     * It stops once the pre-fight overlay has been reached, so it cannot fight a later screen
     * that happens to keep a small number in the same word. */
    mode_force_tick();

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

    if (menu_click_debug() && LD32(GX_CLICK)) {
        menu_click_seen++;
        fprintf(stderr, "menu-click: frame %ld -- the menu is entered with click=1 at "
                        "mouse=(%d,%d), screen=%u\n",
                hostwin_frames(), (int32_t)LD32(GX_MOUSE_X), (int32_t)LD32(GX_MOUSE_Y),
                LD32(GX_SCREEN));
    }

    bg_table_report();           /* LF2_BG_TABLE=1: the loaded stage's layers, once */
    modemenu_mouse();            /* pointer -> selection on the post-load mode menu */
    overlay_mouse();             /* pointer -> selection on the pre-fight overlay */
    /* Not while the overlay is up: it sits ON character selection, so both would take the
     * same pointer and the slot cursor would wander while the player aims at "Fight!". */
    if (!overlay_open()) charselect_mouse();

    fn_004246b0__orig();
    startup_after_game_frame(self, mode);
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
