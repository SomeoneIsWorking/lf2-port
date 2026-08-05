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
void fn_00419a60__orig(void);
void fn_0043f010__orig(void);
void fn_00423940__orig(void);

/* Overridden so far:
 *
 *   fn_004246b0   the front-end menu -- selection index, so a controller can drive it
 *   fn_00423b00   element draw -- declines the advertising panel by its descriptor
 *   fn_0043f010   clip draw -- declines the ad system's corner update notice
 *   fn_00419a60   per-frame player input -- merges a controller into the game's buttons
 *
 * A note from the first attempt at the ads, so it is not repeated: fn_004242e0 looked like
 * the ad strip -- it references http://www.littlefighter.com/advertise and ShellExecute's
 * "open" verb, and its hit-test constants (x 145..775, y 535) match the "To advertise on
 * LF2" link exactly. It is not. It is a general drawing helper, called seven times from the
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

/* The pre-fight overlay on the character-select screen: Fight! / Reset All / Reset Random
 * / Background / Difficulty / Exit, index 0..5, up decrements and wraps. Located by
 * diffing .data across a single d-pad press rather than by reading fn_0041bc90, where a
 * search for the compare returned 20+ indistinguishable candidates; confirmed by matching
 * 2 -> 1 against the frame where the highlight moved Reset Random -> Reset All. */
enum { OVERLAY_SEL = 0x0044d06c };

/* 0x0044d070 was used here as an "which screen is up" word, derived from stage-mode .data
 * dumps where it reads -100 while players pick, 0 while the overlay is up and 1 in the
 * match. It is the GAME MODE, not the screen: in VS mode it reads 1 with the overlay open,
 * so the overlay took no mouse input at all there and only stage mode ever worked. It was
 * derived from stage-mode dumps and checked against stage-mode dumps, which is why it
 * looked perfect. The screen is now taken from what the game DRAWS -- see panel_overlay_up()
 * in runtime/ddraw.c -- and this word is deliberately not used. */

/* The ad system's update notice in the top-right corner; see fn_0043f010 below. */
enum { MENU_CLIP7 = 0x00451188 };            /* sheet handle, loaded from "MENU_CLIP7" */
enum { CURSOR_SHEET = 0x00451170 };          /* sheet handle of the game's own mouse cursor */
enum { NOTICE_X = 725, NOTICE_Y = 5 };       /* the game's own constants for the notice */

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
enum { MODE_ENTER = 1, MODE_IN_GAME = 2 };

static const struct { uint32_t screen; const Item *items; int n; } SCREENS[] = {
    { 0, MAIN_MENU,        (int)(sizeof MAIN_MENU / sizeof MAIN_MENU[0]) },
    { 6, CONTROL_SETTINGS, (int)(sizeof CONTROL_SETTINGS / sizeof CONTROL_SETTINGS[0]) },
    { 7, RECORDING_INFO,   (int)(sizeof RECORDING_INFO / sizeof RECORDING_INFO[0]) },
};

static int menu_index;
static int menu_confirm_frames;
/* Set when a mouse click should read as the attack button for one gather. */
int mouse_confirm_frames;
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
void charselect_mouse(void);
void modemenu_mouse(void);
void overlay_mouse(void);
int  overlay_open(void);
int hostwin_pointer(int *x, int *y);        /* win32.c */
int hostwin_mouse_clicked(void);            /* win32.c: one-shot, per press */

/* LF2_WIDESCREEN=<w>[x<h>] -- a real wider field of view, not a stretched picture.
 *
 * The distinction is the whole feature. Enlarging only the window makes the game scale its
 * 794-wide composition up, which is the same picture with bigger pixels. What is wanted is
 * MORE WORLD, and that needs the surface the game composes into to be wider AND the game's
 * own idea of its viewport to match.
 *
 * The game keeps its viewport size in .data rather than only as immediates, which is what
 * makes this worth trying at all. Three width/height pairs hold 794/550 at runtime, found
 * by scanning a mid-match .data dump for the literal values:
 *
 *   0x0044d014 / 0x0044d018
 *   0x0044d78c / 0x0044d790
 *   0x00453cd4 / 0x00453cd8
 *
 * All three are written every frame here, because which one the world draw reads is the
 * question this probe exists to answer -- narrowing comes after the effect is seen. */
int lf2_wide_width(void)
{
    static int w = -1;
    if (w < 0) {
        const char *e = getenv("LF2_WIDESCREEN");
        int hh = 0;
        w = 0;
        if (e && sscanf(e, "%dx%d", &w, &hh) >= 1) {
            if (w < 794 || w > 4096) {
                fprintf(stderr, "LF2_WIDESCREEN width %d is outside 794..4096; ignored\n", w);
                w = 0;
            }
        } else if (e) {
            fprintf(stderr, "LF2_WIDESCREEN=\"%s\" is not <w> or <w>x<h>; ignored\n", e);
            w = 0;
        }
    }
    return w;
}

static void wide_apply(void)
{
    const int w = lf2_wide_width();
    if (!w) return;
    static const uint32_t WIDTHS[] = { 0x0044d014, 0x0044d78c, 0x00453cd4 };
    /* LF2_WIDE_ONLY=<i> writes just one of them, which is how the set gets narrowed:
     * writing all three works but says nothing about which one the drawing reads, and a
     * write to something that is NOT a viewport width is exactly how a side effect gets
     * shipped by accident. */
    static int only = -2;
    if (only == -2) { const char *e = getenv("LF2_WIDE_ONLY"); only = e ? atoi(e) : -1; }
    for (unsigned i = 0; i < sizeof WIDTHS / sizeof WIDTHS[0]; i++) {
        if (only >= 0 && (int)i != only) continue;
        if (LD32(WIDTHS[i]) == 794u) ST32(WIDTHS[i], (uint32_t)w);
    }
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

/* ---------------------------------------------------------------------------
 * fn_00419a60 -- per-frame player input.
 *
 * This is where the game turns "a device" into the seven button flags a fighter acts on.
 * Read out of the original:
 *
 *   0x00450b4c[i]   device selector for player i, i in 0..7. <= 0 means the slot is not
 *                   taking live input (unjoined, or -1 while a recording plays back).
 *                   1..4 select a control config; the loop bound is 0x00450b6c.
 *   0x0044fbe0      control configs, stride 80. [+0] is a joystick number, 0 for keyboard;
 *                   [+4..+28] are the seven keyboard codes, [+32..+40] joystick buttons.
 *   this+404        an array of eight player-object pointers, one per slot.
 *   obj+198..204    the previous frame's buttons; obj+205..211 this frame's, one byte each,
 *                   in the order up, down, left, right, attack, jump, defend.
 *   arg3            a byte per player, and 0x0044d040 a global mirror of it; both are
 *                   packed bitmasks written only when recording or networking is on.
 *
 * A controller reaches a player only if that player's control config names a joystick,
 * which means visiting the settings screen before a pad does anything. That is the actual
 * cause of "I plugged in a controller and nothing happened", and it lives here rather than
 * at the winmm boundary -- joyGetPosEx was already answering correctly; nothing asked it.
 *
 * So: the original runs first and fills in the configured devices exactly as it always did,
 * then any connected pad is merged into the buttons of the player it belongs to. Merged,
 * not substituted -- the keyboard keeps working for the same player, which is what makes it
 * "plug it in and play" rather than "now your keyboard is dead".
 *
 * Pads are handed to the live slots in order, so pad 0 drives the first joined player and
 * pad 1 the second, with no configuration at all.
 * ------------------------------------------------------------------------ */

enum { DEVSEL = 0x00450b4c, DEVSEL_END = 0x00450b6c };
enum { PLAYER_PTRS = 404 };            /* this+404: eight player-object pointers */
enum { BTN_CUR = 205 };                /* obj+205..211: this frame's seven buttons */
enum { NET_OR_RECORD = 0x00450b80 };   /* non-zero: the packed masks are being consumed */
enum { RECORDING = 0x0044f1af, MASK_MIRROR = 0x0044d040 };

/* Bit for each of the seven buttons in the packed mask, in button order. The game writes
 * these itself further down its own loop; a pad press has to appear in them too, or a
 * recording made with a controller would replay as a player standing still. */
static const uint8_t BTN_BIT[7] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02 };

/* Counters, not a hit log: the interesting failure is that this never merges anything, and
 * a diagnostic that only printed when it did would be silent in exactly that case. The
 * three together distinguish "no player slot was live", "no controller was bound to one"
 * and "a pad was there and nothing was pressed" -- which are three different bugs. */
static long in_frames, in_live, in_padded, in_presses;

void input_report(void)
{
    fprintf(stderr, "input: %ld gathers, %ld live player-slots, %ld of them with a pad, "
                    "%ld button presses merged\n",
            in_frames, in_live, in_padded, in_presses);
}

/* ---- devices, first come first served ----
 *
 * One keyboard layout (arrows to move, Z attack, X jump, C defend) plus every connected
 * pad form a pool of DEVICES. Outside the game proper every device drives player one, so
 * anyone can work the menus. Inside it (top-level mode 2: character selection and the
 * match) the first device to press anything claims player one, the next player two, and
 * so on; a claim holds until the game returns to the front end. Joining stays the game's
 * own logic -- a claimed device's attack lands in its player's buttons, and that is what
 * fn_0041bc90 already treats as "join".
 *
 * The original gather still runs first for everything that is not a live device: slot
 * bookkeeping, recording playback (device selector -1) and the packed-mask plumbing. Its
 * BUTTONS for live slots are then overwritten, not merged -- the control.txt keyboard
 * layouts are exactly what "only one keyboard layout" removes. */
enum { MAX_DEV = 5 };                    /* the keyboard, then up to four pads */

static int dev_player[MAX_DEV] = { -1, -1, -1, -1, -1 };
static unsigned char dev_prev[MAX_DEV][7];

/* Which player the keyboard claimed, or -1. The mouse drives the same one, so the two
 * never disagree about who they are. */
int keyboard_player(void) { return dev_player[0]; }

static int device_buttons(int dev, unsigned char out[7])
{
    if (dev == 0) {
        /* up, down, left, right, attack, jump, defend -- the game's button order */
        static const uint8_t VKS[7] = { 0x26, 0x28, 0x25, 0x27, 0x5A, 0x58, 0x43 };
        for (int b = 0; b < 7; b++) out[b] = (unsigned char)(hostwin_key_held(VKS[b]) != 0);
        /* A mouse click on a ported menu reads as this device's attack, so the game does
         * its own dispatch, sound and screen change rather than the port simulating them. */
        if (mouse_confirm_frames > 0) { mouse_confirm_frames--; out[4] = 1; }
        return 1;                        /* a keyboard is always there */
    }
    return gamepad_player_buttons(dev - 1, out);
}

void fn_00419a60(void)
{
    const uint32_t self = R(ECX);              /* __thiscall */
    const uint32_t mask_buf = LD32(R(ESP) + 12);

    fn_00419a60__orig();                       /* slots, recording, masks, unchanged */

    const int want_mask   = LD32(NET_OR_RECORD) != 0;
    const int want_mirror = (int8_t)LD8(RECORDING) > 0;
    const int in_game     = top_mode == MODE_IN_GAME;

    in_frames++;

    /* Assignments live only inside the game proper; leaving it (or never having entered)
     * clears them, so the next character selection is first come first served again. */
    if (!in_game)
        for (int d = 0; d < MAX_DEV; d++) dev_player[d] = -1;

    /* Device states this frame, claims on a fresh press. A device's FIRST press both
     * claims the next free player and lands in that player's buttons, so pressing attack
     * on the join screen claims and joins in one stroke. */
    unsigned char btn[MAX_DEV][7];
    int present[MAX_DEV];
    for (int d = 0; d < MAX_DEV; d++) {
        present[d] = device_buttons(d, btn[d]);
        if (!present[d]) { memset(dev_prev[d], 0, 7); continue; }
        if (in_game && dev_player[d] < 0) {
            int fresh = 0;
            for (int b = 0; b < 7; b++) fresh |= btn[d][b] && !dev_prev[d][b];
            if (fresh) {
                int used[4] = { 0, 0, 0, 0 };
                for (int e = 0; e < MAX_DEV; e++)
                    if (dev_player[e] >= 0 && dev_player[e] < 4) used[dev_player[e]] = 1;
                for (int p = 0; p < 4; p++)
                    if (!used[p]) { dev_player[d] = p; break; }
            }
        }
        memcpy(dev_prev[d], btn[d], 7);
    }

    /* LF2_COOP_DEBUG=1 -- the player slot table as the game maintains it, printed whenever
     * it changes: the device selector per slot and the object pointer per slot. This is the
     * ground truth for "can a player join after the stage started" -- a slot going live
     * mid-match would show up here as a selector and a pointer appearing together. Printing
     * on CHANGE only, with the frame, so the log is the transitions rather than a wall. */
    if (getenv("LF2_COOP_DEBUG")) {
        static uint32_t last_sel[8], last_obj[8];
        static int first = 1;
        for (int i = 0; i < 8; i++) {
            const uint32_t sv = LD32(DEVSEL + 4u * (uint32_t)i);
            const uint32_t ov = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
            if (!first && sv == last_sel[i] && ov == last_obj[i]) continue;
            last_sel[i] = sv; last_obj[i] = ov;
            fprintf(stderr, "coop f%ld slot %d: devsel=%d obj=%08x\n",
                    hostwin_frames(), i, (int32_t)sv, ov);
        }
        first = 0;

        /* LF2_COOP_DIFF=<frame> -- what actually distinguishes a slot that is PLAYING from
         * one that is not. Every one of the eight player objects already exists from the
         * moment character selection runs, so joining cannot be an object being created; it
         * has to be a field. This prints the dwords where a playing slot and an idle one
         * differ, which is the shortest path to that field. */
        const char *at = getenv("LF2_COOP_DIFF");
        if (at && hostwin_frames() == atol(at)) {
            const uint32_t a = LD32(self + PLAYER_PTRS + 0);
            const uint32_t b = LD32(self + PLAYER_PTRS + 4u * 4u);
            fprintf(stderr, "coop diff: playing=%08x idle=%08x\n", a, b);
            if (a && b) {
                int n = 0;
                for (uint32_t o = 0; o < 0x420u; o += 4) {
                    const uint32_t va = LD32(a + o), vb = LD32(b + o);
                    if (va == vb) continue;
                    if (++n <= 60)
                        fprintf(stderr, "  +%03x  playing=%-11d idle=%-11d (%08x / %08x)\n",
                                o, (int32_t)va, (int32_t)vb, va, vb);
                }
                fprintf(stderr, "coop diff: %d differing dwords of %d\n", n, 0x420 / 4);
            }
        }
    }

    for (uint32_t sel = DEVSEL, i = 0; sel < DEVSEL_END && i < 4; sel += 4, i++) {
        if ((int32_t)LD32(sel) <= 0) continue;         /* recording or demo: not ours */
        in_live++;

        /* This slot's buttons from OUR devices only: outside the game everything routes
         * to the first slot; inside it, whatever claimed this player. A slot the game
         * has filled with a computer is still a live slot and its AI writes its buttons
         * after this gather, so writing here is harmless -- measured back when pads
         * merged by slot order (see tools/controller_2p_test.sh). */
        unsigned char out[7] = { 0, 0, 0, 0, 0, 0, 0 };
        int fed = 0;
        for (int d = 0; d < MAX_DEV; d++) {
            if (!present[d]) continue;
            const int target = in_game ? dev_player[d] : 0;
            if (target != (int)i) continue;
            fed = 1;
            for (int b = 0; b < 7; b++) out[b] |= btn[d][b];
        }
        if (fed) in_padded++;

        const uint32_t obj = LD32(self + PLAYER_PTRS + 4 * i);
        if (!obj) continue;

        uint8_t mask = 0;
        for (int b = 0; b < 7; b++) {
            ST8(obj + BTN_CUR + b, out[b]);
            if (out[b]) { mask |= BTN_BIT[b]; in_presses++; }
        }
        if (want_mask)   ST8(mask_buf + i, mask);
        if (want_mirror) ST8(MASK_MIRROR + i, mask);
    }
}

/* ---------------------------------------------------------------------------
 * fn_0043f010 -- draw one clip from a sprite sheet, and the update notice.
 *
 * What was left in the top-right corner of the main menu after the ad panel and strips
 * came out: a small "Update on <date>" label at (725,5)-(787,18), drawn every frame, and
 * clickable. It is the ad system's own status line -- the same subsystem that reads
 * data/adinfo.txt and data/ad0.txt (the latter is a list of banner rectangles and
 * click-through URLs). Clicking it opens sub-screen -3, an update page that in this port
 * can never do anything: WININET is stubbed, so there is nothing to fetch.
 *
 * Found with the blit tracer, not by reading: LF2_BLT_RECTS showed exactly one destination
 * in that corner, LF2_BLT_STACK put the call inside the menu, and the menu's own code
 * there is
 *
 *     EnterCriticalSection(&ad_lock);  state = [0x00458424];  LeaveCriticalSection(...)
 *     if (state == 1 || state == 2)  draw clip 0x0b at (725,5)      // busy, no link
 *     else                           draw clip 6/7 at (725,5)       // idle, clickable
 *                                    if (mouse.x >= 725 && mouse.y < 18 && clicked)
 *                                        sub_screen = -3
 *
 * The state is 0 at runtime (measured), so the live case is the clickable one.
 *
 * Identity, not coordinates-as-a-hack: the element is clip 6..9 and 0x0b of MENU_CLIP7 at
 * a position the game itself hard-codes, and nothing else draws there -- the rect scan
 * found one destination in that corner across 500 frames. Declining it here is the same
 * shape as declining the ad panel by its descriptor in fn_00423b00.
 *
 * The honest alternative is porting fn_004246b0's body around that block, which is 4689
 * lines of generated C with no function boundary anywhere near it. That is worth doing
 * eventually; it is not worth doing to remove one label.
 *
 * Args, from the call sites: (x, y, clip, ...), six of them, RET 0x18, ECX = the sheet.
 * ------------------------------------------------------------------------ */

/* The three 8x16 bitmap font sheets fn_00423940 selects between. Any clip drawn from one
 * of these is a text glyph, whatever code path asked for it -- which is the only place all
 * of the game's own text meets. */
enum { FONT_SHEET_0 = 0x0044faf4, FONT_SHEET_1 = 0x0044f888, FONT_SHEET_2 = 0x0044fcbc };

static int font_sheet_index(uint32_t obj)
{
    if (!obj) return -1;
    if (obj == LD32(FONT_SHEET_0)) return 0;
    if (obj == LD32(FONT_SHEET_1)) return 1;
    if (obj == LD32(FONT_SHEET_2)) return 2;
    return -1;
}

void fn_0043f010(void)
{
    /* A clip drawn from a font sheet is a text glyph, and the clip index IS the character
     * code. Tell the blit path, which is the only place that also knows the destination
     * surface, and clear it afterwards so an ordinary sprite is never mistaken for text. */
    const int sheet = font_sheet_index(R(ECX));
    if (sheet >= 0) {
        glyph_hint_set((int32_t)LD32(R(ESP) + 12));
        fn_0043f010__orig();
        glyph_hint_clear();
        return;
    }

    if (R(ECX) == LD32(MENU_CLIP7) &&
        LD32(R(ESP) + 4) == NOTICE_X && LD32(R(ESP) + 8) == NOTICE_Y) {
        R(ESP) += 4 + 24;                    /* RET 0x18: return address and six args */
        return;
    }

    /* The game draws its own mouse cursor -- an 11x19 sprite at the pointer, from its own
     * sheet -- on top of whatever the host is already showing. Two cursors, one of which
     * is not the user's.
     *
     * Identified rather than guessed: LF2_SMALL_BLT listed it as the only 11x19 blit, and
     * it lands at (pointer.x, pointer.y + 2) every time. The correlation hook
     * (LF2_CURSOR_FIND) could not see it, because the blit is issued from inside
     * fn_0043f010, which draws EVERYTHING -- aggregating by call site drowned an 11x19
     * sprite among full-screen backgrounds. Aggregating by call site is the wrong key when
     * one call site draws the whole game.
     *
     * The sheet handle is a heap pointer with no identity across runs, so the test is the
     * .data slot that holds it, found by scanning .data for the handle at the moment of the
     * draw. Three call sites use it (0x428778, 0x424660, 0x4329ea -- front end, mode menu,
     * in game), so declining by sheet removes it on every screen at once rather than one
     * call site at a time.
     *
     * LF2_CURSOR_ON=1 restores it. */
    if (R(ECX) && R(ECX) == LD32(CURSOR_SHEET) && !getenv("LF2_CURSOR_ON")) {
        /* Declining the whole SHEET was wrong: it is shared with the menu's character
         * artwork, and dropping it blanked 51492 pixels down the left of the screen. The
         * cursor is the draw of that sheet that lands ON the pointer, so that is the test.
         * The +2 is the sprite's own vertical offset, measured from the blit. */
        const int ax = (int)LD32(R(ESP) + 4), ay = (int)LD32(R(ESP) + 8);
        if (ax == (int)LD32(GX_MOUSE_X) && ay == (int)LD32(GX_MOUSE_Y) + 2) {
            R(ESP) += 4 + 24;                /* RET 0x18: return address and six args */
            return;
        }
    }

    /* Which call draws the mouse cursor? It reaches Blt as an 11x19 sprite at the
     * pointer, but that blit is issued from inside this function, which draws everything
     * -- so the identity has to come from this call's own arguments and caller. */
    if (getenv("LF2_CURSOR_TRACE")) {
        const int ax = (int)LD32(R(ESP) + 4), ay = (int)LD32(R(ESP) + 8);
        const int mx = (int)LD32(GX_MOUSE_X), my = (int)LD32(GX_MOUSE_Y);
        if (ax >= mx - 4 && ax <= mx + 4 && ay >= my - 4 && ay <= my + 4) {
            static uint32_t seen[8]; static int n;
            const uint32_t ra = LD32(R(ESP));
            int known = 0;
            for (int i = 0; i < n; i++) if (seen[i] == ra) { known = 1; break; }
            if (!known && n < 8) {
                seen[n++] = ra;
                fprintf(stderr, "cursor draw: caller=%08x args x=%d y=%d clip=%d sheet=%08x "
                        "(pointer %d,%d)\n", ra, ax, ay, (int32_t)LD32(R(ESP) + 12), R(ECX), mx, my);
                /* The handle is a heap pointer with no stable identity across runs; the
                 * .data slot that HOLDS it does have one. Find it. */
                for (uint32_t a = 0x0044d000; a < 0x00459724; a += 4)
                    if (LD32(a) == R(ECX))
                        fprintf(stderr, "    sheet handle also lives at .data %08x\n", a);
            }
        }
    }

    /* The loading screen's "To advertise on LF2" link. Its sheet handle is a heap pointer
     * with no stable identity, but the draw has exactly one call site, inside the loading
     * screen's presenter (fn_004242e0) -- so the call site IS the identity. Its click
     * already goes nowhere: ShellExecuteA is a stub. */
    if (LD32(R(ESP)) == 0x0042459a) {
        R(ESP) += 4 + 24;
        return;
    }
    fn_0043f010__orig();
}

/* ---------------------------------------------------------------------------
 * fn_00423940 -- the game's own text, drawn from an 8x16 bitmap sheet.
 *
 * Read out of the body rather than off the call sites, after a first attempt at the latter
 * gave a wrong answer: scanning for pushed CONSTANTS silently drops register-pushed
 * arguments, so the fourth argument appeared to be 0x40 at one site and 0x01 at another
 * when they were not the same argument at all.
 *
 *   fn_00423940(str, x, y, cols, rows, font)
 *
 *     x, y    pixels; the pen advances 8 per glyph and 16 per line
 *     cols    wrap width IN CHARACTERS -- this is a text box, not a single line
 *     rows    maximum lines
 *     font    0, 1 or 2, selecting a sheet at 0x0044faf4 / 0x0044f888 / 0x0044fcbc
 *
 * Each glyph goes out as fn_0043f010(x, y, char_code, 1, 0, sheet) -- the character code
 * IS the clip index into the sheet. fn_00423a70 wraps this, calling it four times at +-1
 * pixel offsets, which is the outline.
 * ------------------------------------------------------------------------ */
void fn_00423940(void)
{
    if (getenv("LF2_GAMETEXT_DEBUG")) {
        char buf[128];
        const uint32_t str = LD32(R(ESP) + 4);
        unsigned n = 0;
        for (; n < sizeof buf - 1; n++) {
            const uint8_t c = LD8(str + n);
            if (!c) break;
            buf[n] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        buf[n] = 0;
        fprintf(stderr, "gametext x=%d y=%d cols=%d rows=%d font=%d \"%s\"\n",
                (int32_t)LD32(R(ESP) + 8), (int32_t)LD32(R(ESP) + 12),
                (int32_t)LD32(R(ESP) + 16), (int32_t)LD32(R(ESP) + 20),
                (int32_t)LD32(R(ESP) + 24), buf);
    }
    fn_00423940__orig();
}

/* ---------------------------------------------------------------------------
 * fn_0043c4a0 -- the ad-set load: reads data/adinfo.txt to choose the current set, then
 * parses data/ad<n>.txt and loads sprite/sys/ad<n>.bmp into the ad system's tables.
 *
 * This is the ROOT of every advertising surface -- the loading screen's grid, the menus'
 * panel and strips, the banner rows are all presenters over these tables, and every one
 * of them already handles the tables being empty. Its one caller is the ad-system init
 * (fn_0043cf40): on a zero return the init falls to fn_0043c690, which resets adinfo.txt
 * to the factory default ("now 0 4") and loads nothing. So returning 0 here empties every
 * surface at once through the game's OWN no-ads fallback -- the same state a machine is
 * in after the (long-dead) ad server fails to answer, verified clean on every screen.
 *
 * Declining draw by draw does not scale: the front menu's panel is element 0x0044d060
 * but the mode menu's is 0x0044d020, and the loading screen draws through fn_004242e0
 * with no element descriptor at all. Those per-draw declines are kept, but this is the
 * fix for all of them at once.
 *
 * (Two near misses, so they are not retried: fn_0043c240 also parses ad<n>.txt but never
 * runs at boot, and fn_0043bec0 -- gating the same init -- is the DirectDraw init check,
 * whose failure path is "DirectDraw Init FAILED".)
 *
 * ABI: no arguments, RET 0; result in EAX, 0 = nothing loaded.
 * ------------------------------------------------------------------------ */
void fn_0043c4a0(void)
{
    if (getenv("LF2_ADS_ON")) { void fn_0043c4a0__orig(void); fn_0043c4a0__orig(); return; }
    R(EAX) = 0;
    R(ESP) += 4;                             /* pop the return address only */
}

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
                    top_mode, have, px, py);
    }
    if (top_mode != MODE_IN_GAME) return;

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
    if (top_mode != MODE_IN_GAME) return;

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
    return top_mode == MODE_IN_GAME && panel_overlay_up()
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

/* ---------------------------------------------------------------------------
 * fn_004148a0 -- decrypt one data file into data\temporary.txt.
 *
 * This is the data load. Every one of the game's ~150 object files goes through it, and
 * the original does it ONE BYTE AT A TIME through the C runtime:
 *
 *     fscanf(in, "%c", &c);  ...  fprintf(out, "%c", c - key[i]);
 *
 * which is fine at native speed and is not fine through a recompiled CPU, where each of
 * those is a guest call into a host import. It came to 2.5 million fscanf calls per load,
 * and it is why every attempt to speed the load up by touching the RENDERING failed: the
 * drawing was measured at 14% of the load (LF2_LOAD_PROF), and this is most of the rest.
 *
 * The cipher, read straight out of the decompiled body rather than guessed:
 *   key    "SiuHungIsAGoodBearBecauseHeIsVeryGood", 37 bytes
 *   header the first 0x7b = 123 bytes are consumed and discarded, and the key index is
 *          advanced once per consumed byte, so the payload starts at key index 123 % 37 = 12
 *   byte   out = (in - key[i]) mod 256, then i = (i + 1) % 37
 *
 * (Index 12 is where "odBearBecauseHeIsVeryGood" starts, which is why the widely circulated
 * 25-character key decrypts the first 25 bytes of a file and then turns to noise -- it is
 * this key seen from its offset, with the wrap missing.)
 *
 * Byte-exactness matters more than speed here, so the two things the CRT does that a naive
 * port would not are both reproduced: the input is opened in TEXT mode, so CRLF collapses
 * to LF before decryption (lf2_open_text), and the output is written raw, which is what the
 * port's own "w" fopen does. Anything that cannot be done -- a missing input, an unwritable
 * output -- falls through to the original body rather than silently producing a short file,
 * because a truncated decrypt would show up as the game quietly missing objects.
 *
 * Calling convention: cdecl. The generated body ends in `R(ESP) += 4`, so the argument is
 * the caller's to pop and only the return address comes off here.
 * ------------------------------------------------------------------------ */
const char *lf2_host_path(const char *guest_style);      /* imports.c */
char       *lf2_read_text(const char *host_path, size_t *len);   /* imports.c */

long decrypt_files, decrypt_bytes;

void fn_004148a0__orig(void);

/* LF2_DECRYPT_DUMP=<dir> copies each decrypted file out as NNN.txt, in order. Run once with
 * LF2_SLOW_DECRYPT=1 and once without, diff the two directories, and the native decrypt is
 * either byte-identical to the game's own on every file or it is not -- which is the only
 * check worth having, since a decrypt that is subtly wrong shows up as the game quietly
 * missing frames rather than as a crash. It has to sit in the override and not in the fast
 * path, or the control run dumps nothing and the diff reads as a pass. */
static void decrypt_dump(void)
{
    const char *dir = getenv("LF2_DECRYPT_DUMP");
    if (!dir || !*dir) return;
    static int n;
    char dst[512];
    snprintf(dst, sizeof dst, "%s/%04d.txt", dir, n++);
    FILE *in = fopen(lf2_host_path("data\\temporary.txt"), "rb");
    if (!in) { fprintf(stderr, "decrypt dump: cannot read the output for %s\n", dst); return; }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); fprintf(stderr, "decrypt dump: cannot write %s\n", dst); return; }
    char b[65536]; size_t got;
    while ((got = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, got, out);
    fclose(in); fclose(out);
}

void fn_004148a0(void)
{
    static int native = -1;
    if (native < 0) native = getenv("LF2_SLOW_DECRYPT") == NULL;
    if (!native) { fn_004148a0__orig(); decrypt_dump(); return; }

    static const char KEY[] = "SiuHungIsAGoodBearBecauseHeIsVeryGood";
    enum { KEYLEN = 37, HEADER = 0x7b };
    _Static_assert(sizeof KEY - 1 == KEYLEN, "the key length is part of the cipher");

    const uint32_t arg = LD32(R(ESP) + 4);
    const char *src = lf2_host_path((const char *)(g_mem + arg));

    size_t n = 0;
    char *buf = lf2_read_text(src, &n);
    if (!buf) { fn_004148a0__orig(); return; }

    FILE *out = fopen(lf2_host_path("data\\temporary.txt"), "w");
    if (!out) { free(buf); fn_004148a0__orig(); return; }

    unsigned ki = HEADER % KEYLEN;
    for (size_t i = HEADER; i < n; i++) {
        const int v = ((int)(unsigned char)buf[i] - (int)(unsigned char)KEY[ki]) & 0xff;
        ki = (ki + 1u) % KEYLEN;
        fputc(v, out);
    }
    decrypt_files++;
    decrypt_bytes += (long)(n > HEADER ? n - HEADER : 0);

    fclose(out);
    free(buf);
    decrypt_dump();
    R(ESP) += 4;                 /* cdecl: the return address only */
}
