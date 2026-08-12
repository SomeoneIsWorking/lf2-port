/* The post-load screens' mouse: mode menu, character selection, the overlay.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "geom.h"

#include "guest_ops.h"
#include "guest_map.h"
#include "hostwin.h"

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
         * the attack that joins -- gated on the screen being DRAWN so it cannot fire
         * during a match, where the same rectangles are just part of the arena.
         *
         * Not on a .data word, which is what this comment used to say: the candidate for
         * one (0x0044d070) is the GAME MODE wearing a screen's disguise, and it reads the
         * same in VS mode whether the overlay is up or not. See menu.c. */
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

/* ---- AN IDLE POINTER MUST NOT COUNT AS A MOVE WHEN A SCREEN OPENS ----
 *
 * Each handler below hovers only when the pointer actually moved, so an idle mouse never
 * fights the keyboard or a pad. That guard was defeated on the FIRST frame of every screen:
 * `last_x/last_y` belong to the handler, not to the screen, so on the frame a screen opened
 * they still held wherever the pointer had been on the previous one -- or -1 -- and `moved`
 * read true against a pointer nobody had touched.
 *
 * It is not cosmetic. Measured on the pre-fight overlay: the mouse route's last click on
 * character selection leaves the pointer at (200,150), which is inside the overlay's panel
 * band. The overlay opened at frame 1751 with the game's own selection on item 2, the idle
 * pointer was read as a move on 1752 and dragged it to item 5 -- Exit -- and the next click
 * activated it. The overlay was gone by frame 1800 and the run never reached a match.
 *
 * So each screen seeds the handler's memory with the pointer's CURRENT position on the frame
 * it opens. A player who then moves the mouse gets hover; one who does not, does not.
 */
static int screen_edge_seed(int *open_last, int open_now, int *last_x, int *last_y,
                            int mx, int my)
{
    const int rising = open_now && !*open_last;
    *open_last = open_now;
    if (rising) { *last_x = mx; *last_y = my; return 0; }
    const int moved = (mx != *last_x || my != *last_y);
    *last_x = mx; *last_y = my;
    return moved;
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

static int modemenu_was_open;

void modemenu_mouse(void)
{
    if (game_top_mode() != MODE_IN_GAME) { modemenu_was_open = 0; return; }

    int mx, my;
    if (!hostwin_pointer(&mx, &my)) return;

    /* IS THE MODE MENU ACTUALLY ON SCREEN. This used to ask `LD32(MODEMENU_SEL) < 8`, and
     * 0x00451160 is the game MODE, not a menu cursor -- FUN_00429730 and the score-board read
     * it as 1/4/5 DURING A MATCH, so the gate was satisfied in a match too and the early-out
     * was not doing the job its name claimed (issue #51). It is the same trap the pre-fight
     * overlay fell into once: a .data flag that is the game mode wearing a screen's disguise.
     *
     * The honest signal is what the game DRAWS -- the mode menu paints its whole screen
     * 0x122565, a literal that appears exactly once in the binary -- which is the identifier
     * this port already uses for per-screen framing and for the `@frontend` route anchor. */
    /* THE DISCRIMINATOR, counted rather than argued: frames on which the OLD gate was true and
     * the new one is false are exactly the frames the handler used to be live on a screen that
     * was not the mode menu. LF2_MODEMENU_DEBUG prints it with its denominator. */
    {
        static long live_frames, wrong_frames;
        const int old_gate = LD32(MODEMENU_SEL) < MODEMENU_ITEMS;
        const int new_gate = panel_modemenu_up();
        live_frames++;
        if (old_gate && !new_gate) wrong_frames++;
        if (getenv("LF2_MODEMENU_DEBUG") && (live_frames % 900) == 0)
            fprintf(stderr, "modemenu: the old game-mode gate was true on %ld of %ld frame(s) "
                            "where the mode menu was NOT drawn -- %s\n",
                    wrong_frames, live_frames,
                    wrong_frames ? "the handler was live off its own screen (issue #51)"
                                 : "so this run shows no misfire, and says nothing about runs "
                                   "that reach other screens");
    }
    if (!panel_modemenu_up()) { modemenu_was_open = 0; return; }
    const uint32_t cur = LD32(MODEMENU_SEL);

    static int last_x = -1, last_y = -1;
    const int moved = screen_edge_seed(&modemenu_was_open, 1, &last_x, &last_y, mx, my);

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
 * THE ROW GEOMETRY IS THE GAME'S OWN, DECOMPILED. Ghidra on FUN_00429730 -- the only
 * function that touches OVERLAY_SEL, 21 times -- gives the highlight draw verbatim:
 *
 *     if (sel == 0) draw_clip(0x5c, 0x10, 9,  ...)     //  92, 16
 *     if (sel == 1) draw_clip(0x40, 0x27, 10, ...)     //  64, 39
 *     if (sel == 2) draw_clip(0x28, 0x40, 0xb, ...)    //  40, 64
 *     if (sel == 3) draw_clip(0x0f, 0x57, 0xc, ...)    //  15, 87
 *     if (sel == 4) draw_clip(0x25, 0x6f, 0xd, ...)    //  37, 111
 *     if (sel == 5) draw_clip(0x65, 0x89, 0xe, ...)    // 101, 137
 *
 * So the rows are at 16, 39, 64, 87, 111, 137 -- NOT the uniform 24 from 16 this used to
 * assume, and the labels are staggered in x as well (the list is drawn on a slant). The old
 * formula `(y - 16) / 24` gives 16, 40, 64, 88, 112, 136: right at the ends and up to a row
 * out in the middle, which is a click landing on the wrong item and no way to notice by
 * looking at it.
 *
 * That is the difference between measuring where a highlight landed and reading the code
 * that put it there. The blit measurement was not wrong about the three rows it sampled --
 * it sampled 0, 2 and 5, which happen to be the three the uniform step gets nearly right.
 *
 * The x band stays the whole panel (blitted at (3,3)-(307,169)) rather than each label's own
 * left edge: a player aiming at a slanted list should not have to hit the glyphs, and the y
 * alone identifies the row unambiguously.
 * ------------------------------------------------------------------------ */
enum { OVERLAY_ITEMS = GEOM_OVERLAY_ITEMS };

/* The overlay's edge memory, cleared when it is not up so the next opening is a rising edge
 * again. Separate from the handler so the early return can reach it. */
static int overlay_was_open;
static void overlay_mouse_closed(void) { overlay_was_open = 0; }

int overlay_open(void)
{
    return game_top_mode() == MODE_IN_GAME && panel_overlay_up()
        && LD32(OVERLAY_SEL) < OVERLAY_ITEMS;
}

void overlay_mouse(void)
{
    if (!overlay_open()) { overlay_mouse_closed(); return; }

    int mx, my;
    if (!hostwin_pointer(&mx, &my)) return;

    static int last_x = -1, last_y = -1;
    const int moved = screen_edge_seed(&overlay_was_open, 1, &last_x, &last_y, mx, my);

    const int item = geom_overlay_item_at(mx, my);
    if (getenv("LF2_OVERLAY_DEBUG"))
        fprintf(stderr, "overlay: frame %ld pointer (%d,%d) -> item %d, selection %u\n",
                hostwin_frames(), mx, my, item, LD32(OVERLAY_SEL));
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

/* ---------------------------------------------------------------------------
 * EXIT TO MENU -- leaving a match without killing the process (issue #22).
 *
 * The port does NOT reset its own state to fake this. The game has its own way out of a
 * match and that is what gets driven: F4. Verified by pressing it in a scripted run and
 * looking at the result -- scratch/exit22/frame_002400.png is the character-select screen
 * with the pre-fight overlay open, from a run that was in a fight forty frames earlier.
 *
 * That screen is one item short of the front end, and the item is the overlay's own Exit
 * (index 5 of OVERLAY_SEL). So the second step places that selection and lets the GAME's
 * attack button dispatch it, exactly as a mouse click on the overlay already does -- the
 * sound, the state change and the screen it goes to stay the game's.
 *
 * WHOSE attack, and why it is not the keyboard's: inside the game proper a device's buttons
 * only reach the game through the player slot it claimed, so a synthetic press from a device
 * with no slot goes nowhere. That is not a guess -- a scripted run pressing the keyboard's
 * own arrows and attack at this overlay moved the selection not at all
 * (scratch/exit22d/frame_002520.png still has "Fight!" highlighted), because the keyboard
 * had claimed no slot in it. The device that opened the pause menu was playing, so it has
 * one, and the confirm is issued through that.
 *
 * The whole thing must happen with the game UNFROZEN: the pause menu works by declining to
 * call fn_004246b0__orig, so nothing the game does can advance while it is up. The caller
 * unpauses first, and the state machine below then runs a frame at a time.
 * ------------------------------------------------------------------------ */
enum { OV_EXIT = 5 };                     /* the overlay's own Exit item */
enum { EXIT_KEY_HOLD = 6 };               /* frames F4 is held, matching the key scripts */
enum { EXIT_SETTLE = 10 };                /* frames the overlay is left to appear properly */
enum { EXIT_GIVEUP = 400 };               /* about seven seconds; then say so and stop */

static int  exit_state;                   /* 0 idle, 1 F4 sent, 2 overlay seen, 3 probing */
static void exit_probe(long f);
void exit_probe_tick(long f);
static long exit_probe_watch_from = -1;
static int  exit_probe_saw_menu;
static int  exit_probe_said;
static int  exit_dev = -1;
static long exit_since, exit_overlay_at;

void exit_to_menu_begin(int dev)
{
    /* The confirm has to come from a device that is DRIVING A PLAYER, or it lands nowhere
     * (see the note above). The device that opened the pause menu usually is one; a pause
     * opened from the keyboard by someone playing on a pad is not, so the press is issued
     * through whoever is playing instead, and which it was is said out loud. */
    if (device_player(dev) < 0) {
        const int other = any_playing_device();
        if (other >= 0) {
            fprintf(stderr, "exit to menu: device %d is driving no player, so its press "
                            "would land nowhere; the confirm is issued through device %d, "
                            "which is driving player %d\n",
                    dev, other, device_player(other));
            dev = other;
        } else {
            fprintf(stderr, "exit to menu: NO device is driving a player, so nothing can "
                            "confirm the overlay's Exit item. F4 is still sent -- the match "
                            "ends and the player is left on the game's own screen -- but "
                            "this will not reach the front end on its own\n");
        }
    }
    exit_dev = dev;
    exit_state = 1;
    exit_since = hostwin_frames();
    exit_overlay_at = -1;
    hostwin_inject_key(0x73, 1);          /* F4 -- the game's own way out of a match */
    fprintf(stderr, "exit to menu: F4 sent at frame %ld on behalf of device %d; waiting for "
                    "the game's own post-match overlay\n", exit_since, dev);
}

void exit_to_menu_tick(void)
{
    /* BEFORE the early-out, and that is the whole point: exit_probe zeroes exit_state when it
     * writes, so a watch armed there and ticked below this line never ran at all -- seven
     * candidate runs printed their write and then said NOTHING, which reads as "no verdict"
     * rather than as "the instrument is dead". */
    exit_probe_tick(hostwin_frames());
    if (!exit_state) return;
    const long f = hostwin_frames();

    if (exit_state == 1 && f - exit_since >= EXIT_KEY_HOLD) hostwin_inject_key(0x73, 0);

    if (f - exit_since > EXIT_GIVEUP) {
        fprintf(stderr, "exit to menu: GAVE UP after %d frames -- the overlay %s. The match "
                        "was left as it was rather than the port forcing a transition it "
                        "could not drive\n",
                EXIT_GIVEUP, exit_overlay_at >= 0 ? "appeared but the Exit item never took"
                                                  : "never appeared, so F4 did not land");
        exit_state = 0;
        return;
    }

    if (exit_state == 3) { exit_probe(f); return; }
    if (!overlay_open()) return;
    if (exit_state == 1) {
        exit_state = 2;
        exit_overlay_at = f;
        fprintf(stderr, "exit to menu: the overlay is up at frame %ld\n", f);
        return;
    }
    if (f - exit_overlay_at < EXIT_SETTLE) return;

    ST32(OVERLAY_SEL, (uint32_t)OV_EXIT);
    input_synth_confirm(exit_dev, 2);
    fprintf(stderr, "exit to menu: overlay selection set to Exit (item %d) and device %d's "
                    "attack issued -- the game dispatches it from here\n", OV_EXIT, exit_dev);
    exit_state = 3;
    exit_overlay_at = f;
}

/* ---- LF2_EXIT_PROBE: which .data word sends the game back to its mode menu (issue #22) ----
 *
 * A DIAGNOSTIC, and it stays one. The .data diff between the mode menu and the screen this
 * exit reaches leaves a short list of words that read zero at the mode menu, and a diff can
 * only ever produce suspects -- the test is to write one and watch. This writes the words
 * named in LF2_EXIT_PROBE (comma-separated hex guest addresses) once, a few frames after the
 * exit completes, so a candidate costs a run rather than a rebuild.
 *
 * It says what it wrote and what was there, because a probe that writes a word already zero
 * proves nothing and must not read as a negative result. */
enum { EXIT_PROBE_WAIT = 30 };

static void exit_probe(long f)
{
    if (f - exit_overlay_at < EXIT_PROBE_WAIT) return;
    exit_state = 0;
    const char *spec = getenv("LF2_EXIT_PROBE");
    if (!spec) return;
    if (panel_hud_up()) {
        fprintf(stderr, "exit probe: the match is still up at frame %ld, so nothing was "
                        "written -- this run tested NOTHING\n", f);
        return;
    }
    int wrote = 0;
    for (const char *p = spec; *p; ) {
        char *end = NULL;
        const unsigned long addr = strtoul(p, &end, 16);
        if (end == p) break;
        /* `addr` writes zero; `addr=value` writes that value. The second form is not a
         * convenience -- a probe that can only write ZERO cannot test a candidate whose
         * mode-menu value is non-zero, and the fresh diff has four of those (00450b90=1,
         * 00451160=1, 00451224=20, 00458580=35). Six candidates came back negative before this
         * existed, and for the non-zero ones that negative meant nothing at all. */
        unsigned long val = 0;
        if (*end == '=') { const char *q = end + 1; char *e2 = NULL;
                           val = strtoul(q, &e2, 0); if (e2 != q) end = e2; }
        const uint32_t was = LD32((uint32_t)addr);
        ST32((uint32_t)addr, (uint32_t)val);
        fprintf(stderr, "exit probe: [%08lx] was %u, wrote %lu at frame %ld%s\n",
                addr, was, val, f,
                was == (uint32_t)val ? "  -- IT ALREADY HELD THAT VALUE, so this proves nothing"
                                     : "");
        wrote++;
        p = (*end == ',') ? end + 1 : end;
    }
    if (!wrote)
        fprintf(stderr, "exit probe: LF2_EXIT_PROBE=\"%s\" named no address I could parse, so "
                        "NOTHING was written\n", spec);
    if (wrote) exit_probe_watch_from = f;
}

/* THE VERDICT, which this probe never had. Judging it used to mean diffing the frame after the
 * write against a frame of the mode menu -- and issue #22 records how that went: the two screens
 * share a blit destination and the "mode menu" side of the comparison was character selection,
 * so the positive control read 0.0% and six candidates were written up as negatives when they
 * were untested.
 *
 * panel_modemenu_up() (issue #51) removes the diffing entirely: the mode menu is identified by
 * the full-screen colour only it paints. So the probe can simply ASK whether the screen it is
 * trying to reach came up, for a while after the write, and say so. */
enum { EXIT_PROBE_WATCH = 240 };

void exit_probe_tick(long f)
{
    /* SELFTEST: arm the watch at frame 0 instead of after an exit. The mode menu is drawn at
     * frame 5 of every run (issue #59), so it falls inside the window and the POSITIVE branch
     * MUST print. Without this the probe has only ever been run against one class -- seven
     * candidates in a row reported "did not appear", which is indistinguishable from a verdict
     * that cannot say anything else. This is the case that makes the negatives mean something. */
    if (exit_probe_watch_from < 0 && getenv("LF2_EXIT_PROBE_SELFTEST")) exit_probe_watch_from = 0;
    if (exit_probe_watch_from < 0) return;
    if (panel_modemenu_up()) exit_probe_saw_menu = 1;
    if (exit_probe_said) return;
    if (f - exit_probe_watch_from >= EXIT_PROBE_WATCH) {
        exit_probe_said = 1;
        if (exit_probe_saw_menu)
            fprintf(stderr, "exit probe: THE MODE MENU CAME UP within %d frame(s) of the write "
                            "-- this candidate reaches it\n", EXIT_PROBE_WATCH);
        else
            fprintf(stderr, "exit probe: the mode menu did NOT appear in the %d frame(s) after "
                            "the write. That is a real negative for this candidate ONLY if the "
                            "write happened -- read the line above, which says so when the word "
                            "was already zero\n", EXIT_PROBE_WATCH);
    }
}
