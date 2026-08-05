/* Hand-written native replacements for recompiled functions.
 *
 * Each function here provides the symbol fn_<addr> that the lifter would otherwise have
 * generated, for an address listed in re/overrides.txt. They run in the guest ABI: the
 * caller's arguments are on the guest stack, and a stdcall callee pops them.
 */
#include "guest_ops.h"
#include "guest_map.h"
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

/* Which players have joined, as a bitmask -- bit i is player i. Found by diffing .data
 * across a character-select join (0 -> 1) and again across a second join (1 -> 3), which is
 * what tells a mask from a count. */
enum { JOINED_MASK = 0x00451288 };

/* this+404: FOUR HUNDRED object pointers, not eight. The eight player slots are its first
 * eight entries; the fighter the game gives a computer opponent lands further up the same
 * array on the same 0x420 stride. Every entry is a live pointer to a pre-allocated record
 * from the moment the data loads, so an object's existence is not its pointer being there.
 *
 * What decides existence is EXISTS: a byte per index at this+4, and `fn_004064d0` walks the
 * table as
 *
 *     ESI = this + 404
 *     EAX = LD32(ESI)                  // obj = table[k]
 *     if (obj->0x338 > 0) obj->0x338--;   // a countdown, run for every entry
 *     if (LD8(this + 4 + k) != 1) goto next
 *
 * -- which is why an idle entry is read exactly once a frame (the countdown) and nothing
 * more, and why filling in a record and setting the joined-players mask was never going to
 * be enough on its own. */
enum { PLAYER_PTRS = 404, TABLE_N = 400, OBJ_STRIDE = 0x420 };
enum { EXISTS = 0x00458b04 };          /* this+4: one byte per object index, 1 = exists */

/* this+2004: a pointer to the object-data registry -- an array of pointers to per-object
 * data blocks, with its entry count at a fixed (large) offset from the base. Field 1780 of
 * a block is the object id from data.txt. Both offsets are the game's own, read off the
 * spawn inlined in fn_0041bc90. */
enum { REG_PTR = 2004, REG_COUNT_OFF = 81273728 };
enum { BTN_CUR = 205 };                /* obj+205..211: this frame's seven buttons */
enum { NET_OR_RECORD = 0x00450b80 };   /* non-zero: the packed masks are being consumed */
enum { RECORDING = 0x0044f1af, MASK_MIRROR = 0x0044d040 };

/* Bit for each of the seven buttons in the packed mask, in button order. The game writes
 * these itself further down its own loop; a pad press has to appear in them too, or a
 * recording made with a controller would replay as a player standing still. */
static const uint8_t BTN_BIT[7] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02 };

/* ---- LF2_COOP_REFS: who POINTS AT a player record ----
 *
 * The question this answers is the one thing left before drop-in coop is mechanical: a
 * fully initialised player record plus its bit in the joined mask is NOT enough to put a
 * fighter in the world (measured -- see issue #15), so there is a list of active objects a
 * fighter also has to be in. A list of objects is a list of POINTERS to them, so the list
 * is found by scanning memory for the pointer values the game already has.
 *
 * It scans every region a guest pointer can live in and reports each aligned dword that
 * equals one of the eight player-record pointers, with the dwords around it -- an array
 * shows up as several hits one stride apart, and a single field in a struct does not.
 *
 * The negative is designed first, because "no references found" is the result that would
 * be believed without evidence. The report always states the regions and their byte
 * counts, the number of dwords compared, a per-target hit count, and the blind spots the
 * method has (unaligned or tagged pointers; the VRAM and PCM arenas, which are ours and
 * hold no game structures). A scan of a zero-length heap prints REFUSED rather than "0
 * hits", because those are not the same answer.
 *
 * And it carries a positive control that cannot be skipped: the eight pointers are read
 * out of `this+404`, so the scan MUST find each non-null one at exactly that address. If
 * it does not, the scan is not seeing memory it claims to see, and the line says FAILED
 * instead of a hit count. A scan that finds nothing and passes its control is evidence;
 * one that finds nothing and never checked is not. */
uint32_t guest_heap_used(void);          /* imports.c */

struct refs_region { const char *name; uint32_t lo, hi; };

static void coop_refs_scan(uint32_t self)
{
    enum { NTARGET = 12 };                /* eight slots, plus up to four extras */
    uint32_t target[NTARGET];
    char label[NTARGET][16];
    long hits[NTARGET];
    int ntarget = 0, nslot = 0;

    for (int i = 0; i < 8; i++) {
        const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
        if (!p) continue;
        target[ntarget] = p;
        snprintf(label[ntarget], sizeof label[0], "slot%d", i);
        hits[ntarget] = 0;
        ntarget++;
        nslot++;
    }

    /* LF2_COOP_REFS_ADDR=<hex>[,...] -- chase an address the scan itself turned up, such
     * as the 1052-byte object at 0x25f149a0 that the "Fight!" heap diff left unexplained. */
    const char *extra = getenv("LF2_COOP_REFS_ADDR");
    for (const char *s = extra; s && *s && ntarget < NTARGET; ) {
        char *end;
        const unsigned long v = strtoul(s, &end, 16);
        if (end == s) break;
        target[ntarget] = (uint32_t)v;
        snprintf(label[ntarget], sizeof label[0], "extra%d", ntarget - nslot);
        hits[ntarget] = 0;
        ntarget++;
        s = (*end == ',') ? end + 1 : end;
    }

    const uint32_t heap_used = guest_heap_used();
    const struct refs_region regions[] = {
        { "image", g_image_lo,       g_image_hi },
        { "heap",  GUEST_HEAP_BASE,  GUEST_HEAP_BASE + heap_used },
        { "stack", GUEST_STACK_BASE, GUEST_STACK_END },
    };
    const int nregion = (int)(sizeof regions / sizeof regions[0]);

    fprintf(stderr, "coop refs: frame %ld, this=%08x, %d targets\n",
            hostwin_frames(), self, ntarget);
    for (int t = 0; t < ntarget; t++)
        fprintf(stderr, "  target %-7s %08x\n", label[t], target[t]);

    if (ntarget == 0) {
        fprintf(stderr, "coop refs: REFUSED -- every player pointer at this+404 is null, so "
                        "there is nothing to look for. This is not a negative result; the "
                        "scan was pointed at a frame that is not a match.\n");
        return;
    }

    long dwords = 0;
    int printed = 0, total_hits = 0;
    for (int r = 0; r < nregion; r++) {
        const uint32_t lo = regions[r].lo, hi = regions[r].hi;
        if (hi <= lo) {
            fprintf(stderr, "coop refs: region %-5s REFUSED -- empty range [%08x,%08x). "
                            "Nothing in it was compared.\n", regions[r].name, lo, hi);
            continue;
        }
        fprintf(stderr, "coop refs: region %-5s [%08x,%08x) %.1f MiB\n",
                regions[r].name, lo, hi, (double)(hi - lo) / (1024.0 * 1024.0));
        for (uint32_t a = lo & ~3u; a + 4 <= hi; a += 4) {
            const uint32_t v = LD32(a);
            dwords++;
            for (int t = 0; t < ntarget; t++) {
                if (v != target[t]) continue;
                hits[t]++;
                total_hits++;
                if (printed++ < 80) {
                    fprintf(stderr, "  hit %08x = %s (%s)  ctx:", a, label[t], regions[r].name);
                    for (int k = -4; k <= 4; k++) {
                        const uint32_t ca = a + 4u * (uint32_t)k;
                        if (ca < lo || ca + 4 > hi) { fprintf(stderr, " --------"); continue; }
                        fprintf(stderr, "%c%08x", k == 0 ? '[' : ' ', LD32(ca));
                    }
                    fprintf(stderr, "\n");
                }
            }
        }
    }

    /* The positive control. Every non-null slot pointer is stored at this+404+4i, so the
     * scan has to have found it there -- unless `this` is outside every region above, which
     * is itself something the scan must say out loud rather than pass over. */
    int control_ok = 1, control_checked = 0;
    for (int i = 0; i < 8; i++) {
        const uint32_t at = self + PLAYER_PTRS + 4u * (uint32_t)i;
        if (!LD32(at)) continue;
        int covered = 0;
        for (int r = 0; r < nregion; r++)
            if (regions[r].hi > regions[r].lo && at >= regions[r].lo && at + 4 <= regions[r].hi)
                covered = 1;
        if (!covered) { control_ok = 0; continue; }
        control_checked++;
    }
    /* Being covered is necessary; having been counted is the actual check. Each covered
     * slot pointer contributes at least the one hit at this+404+4i. */
    for (int t = 0; t < nslot && control_ok; t++)
        if (hits[t] < 1) control_ok = 0;

    fprintf(stderr, "coop refs: %ld dwords compared across %d regions, %d hits\n",
            dwords, nregion, total_hits);
    if (printed > 80)
        fprintf(stderr, "coop refs: %d hits were not printed\n", printed - 80);
    for (int t = 0; t < ntarget; t++)
        fprintf(stderr, "  %-7s %08x  %ld reference%s\n",
                label[t], target[t], hits[t], hits[t] == 1 ? "" : "s");
    if (control_ok && control_checked)
        fprintf(stderr, "coop refs: control PASSED -- all %d slot pointers were found at "
                        "this+404, so the scan does see the memory it reports on\n",
                control_checked);
    else
        fprintf(stderr, "coop refs: control FAILED (%d slot pointers inside a scanned "
                        "region) -- the counts above are NOT evidence of anything\n",
                control_checked);
    fprintf(stderr, "coop refs: blind spots -- unaligned or tagged pointers, an object held "
                    "only as base+offset, and the VRAM/PCM arenas (ours, no game structures). "
                    "A zero count means no ALIGNED dword in the regions above holds that "
                    "value.\n");
}

/* ---- LF2_COOP_TABLE: the object table at this+404, past the eight player slots ----
 *
 * What LF2_COOP_REFS established: the eight player records are pointed at from exactly ONE
 * place each -- consecutive dwords at 0x00458c94 in .data -- and from NOWHERE in 101.9 MiB
 * of heap. So there is no separate heap list of active objects; this table is the list, and
 * it does not stop at eight. The dwords after slot 7 continue on the same 0x420 stride, and
 * the object the "Fight!" heap diff could not place, 0x25f149a0, is entry 11 of it.
 *
 * This walks the table and prints, per entry, the pointer, its index on the stride grid,
 * and the fields the playing-vs-idle diff in issue #15 identified: the chosen character at
 * +0x364, HP at +0x2fc, position at +0x10/+0x18, and +0x354 (99 idle / 0 playing).
 *
 * The negative, again first: the walk states how many entries it examined and why it
 * stopped, and every entry is printed -- including null and off-grid ones -- rather than
 * only the ones that look like objects. A table dump that silently skipped what it could
 * not parse would make a short table and a misread one look the same. */
/* An entry that has never been put into the world reads exactly as the game initialised it:
 * no character, full HP, the origin, and 99 at +0x354. That signature is not a guess -- it
 * is what LF2_COOP_DIFF prints for an idle slot beside a playing one, in the same run. Any
 * departure from it is an object that something has touched. */
static int coop_entry_live(uint32_t p)
{
    return (int32_t)LD32(p + 0x364) != 0     /* chosen character */
        || (int32_t)LD32(p + 0x2fc) != 500   /* HP */
        || (int32_t)LD32(p + 0x10)  != 0     /* x */
        || (int32_t)LD32(p + 0x18)  != 0     /* y */
        || (int32_t)LD32(p + 0x354) != 99;
}

/* LF2_COOP_PAIR=<i>,<j> -- the dwords where table entry i differs from entry j. The existing
 * LF2_COOP_DIFF compares slot 0 against slot 4, which mixes two questions: a player record
 * differs from an idle one both by being a fighter in the world AND by being a player. This
 * takes any two indices, so a live NON-player entry (the computer fighter the game itself
 * put in the table) can be compared against an untouched neighbour of the same kind, which
 * isolates "is in the world" from "is a player". */
static void coop_pair_diff(uint32_t self, int i, int j)
{
    const uint32_t a = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
    const uint32_t b = LD32(self + PLAYER_PTRS + 4u * (uint32_t)j);
    fprintf(stderr, "coop pair: frame %ld, [%d]=%08x (%s) vs [%d]=%08x (%s)\n",
            hostwin_frames(), i, a, a && coop_entry_live(a) ? "LIVE" : "idle",
            j, b, b && coop_entry_live(b) ? "LIVE" : "idle");
    if (!a || !b) {
        fprintf(stderr, "coop pair: REFUSED -- an entry is null, nothing was compared\n");
        return;
    }
    if (!(a && coop_entry_live(a)) && !(b && coop_entry_live(b)))
        fprintf(stderr, "coop pair: WARNING -- NEITHER entry is live, so any difference "
                        "below is between two untouched records and says nothing about "
                        "being in the world\n");
    int n = 0;
    for (uint32_t o = 0; o < 0x420u; o += 4) {
        const uint32_t va = LD32(a + o), vb = LD32(b + o);
        if (va == vb) continue;
        n++;
        fprintf(stderr, "  +%03x  [%d]=%-11d [%d]=%-11d (%08x / %08x)\n",
                o, i, (int32_t)va, j, (int32_t)vb, va, vb);
    }
    fprintf(stderr, "coop pair: %d differing dwords of %d compared\n", n, 0x420 / 4);
}

/* ---- LF2_COOP_SPAWN=<dst>[,<src>] -- put a fighter in the world by imitating the game ----
 *
 * It builds the fighter the way the GAME does, not by copying a neighbour. A clone was the
 * right probe for "is the gate the gate" -- it answered that -- but it can only ever
 * duplicate a fighter already on the stage, because it copies the source's character, HP
 * and everything else. The real sequence is inlined in fn_0041bc90 around 0x004211db:
 *
 *     reg   = LD32(this + 2004)                   // the object-data registry
 *     count = LD32(reg + 81273728)
 *     find i with LD32(LD32(reg + 4i) + 1780) == <the object id wanted>
 *     obj = LD32(this + 404 + 4k)
 *     ECX = obj; fn_004061d0()                    // __thiscall reset of the record
 *     obj->872 = LD32(reg + 4i)                   // the object-data pointer
 *     obj->796 = data->144
 *     this[4 + k] = 1                             // the gate
 *     obj->16 / +20 / +24 and the doubles at +88 / +96 / +104 <- position
 *
 * Field 1780 of a data block IS the object id from data.txt, and that is checked rather
 * than assumed: all 65 registry entries carry an id that appears in the game's own
 * data.txt, with no exceptions, and the only two data.txt ids NOT in the registry are 3 and
 * 12, which are backgrounds rather than objects. It also settles fn_004064d0's comparison
 * of the same field against 7 and 8 -- those are Firen and Freeze, not a type code.
 *
 * Position is copied from a live fighter and offset. That is not a leftover of the clone:
 * it is what the game's own spawn site does, copying +16/+20/+24 and the doubles from
 * another object.
 *
 * NOT ESTABLISHED, and written here rather than hidden: what +0x354 means. fn_004061d0
 * resets it to 99, another spawn site copies it from the spawning object, and the computer
 * opponent the game itself put at entry 11 holds 11 -- its own index. Setting it to the
 * destination index is an imitation of the one in-match registration that can be observed,
 * not a rule derived from the code, and it is what gives the new fighter its HUD bar.
 *
 * It then WATCHES the record for the following frames rather than declaring victory on the
 * write. The interesting failure is not "nothing appeared on screen" -- it is the record
 * being reset by the game's own sweep a frame later, which looks identical from outside and
 * means something quite different. */
static uint32_t spawn_dst_obj;
static int spawn_dst_idx = -1;
static long spawn_frame;

void fn_004061d0(void);                  /* __thiscall: reset the object record in ECX */

/* The registry entry whose data block carries object id `id`, or 0. Refuses out loud
 * rather than returning 0 for two different reasons. */
static uint32_t coop_data_for_id(uint32_t self, int id)
{
    const uint32_t reg = LD32(self + REG_PTR);
    if (!reg || reg < GUEST_HEAP_BASE || reg >= GUEST_HEAP_END) {
        fprintf(stderr, "coop spawn: REFUSED -- this+%d = %08x is not a heap pointer, so the "
                        "registry was never read\n", REG_PTR, reg);
        return 0;
    }
    const int32_t count = (int32_t)LD32(reg + (uint32_t)REG_COUNT_OFF);
    if (count <= 0 || count > 512) {
        fprintf(stderr, "coop spawn: REFUSED -- registry count reads %d, which is not "
                        "plausible; nothing was searched\n", count);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        const uint32_t d = LD32(reg + 4u * (uint32_t)i);
        if (d >= GUEST_HEAP_BASE && d < GUEST_HEAP_END && (int32_t)LD32(d + 1780) == id)
            return d;
    }
    fprintf(stderr, "coop spawn: REFUSED -- no data block with object id %d among the %d "
                    "registry entries (they were ALL examined)\n", id, count);
    return 0;
}

static void coop_spawn(uint32_t self, int dst, int id, int posref, int sel)
{
    const uint32_t d = LD32(self + PLAYER_PTRS + 4u * (uint32_t)dst);
    const uint32_t ref = posref >= 0 ? LD32(self + PLAYER_PTRS + 4u * (uint32_t)posref) : 0;
    if (!d) {
        fprintf(stderr, "coop spawn: REFUSED -- table entry %d is null, nothing written\n", dst);
        return;
    }
    if (LD8(EXISTS + (uint32_t)dst)) {
        fprintf(stderr, "coop spawn: REFUSED -- entry %d already has its gate byte set, so a "
                        "fighter appearing there would prove nothing\n", dst);
        return;
    }
    if (!ref) {
        fprintf(stderr, "coop spawn: REFUSED -- no live fighter to take a spawn position "
                        "from, so this frame is not a match\n");
        return;
    }
    const uint32_t data = coop_data_for_id(self, id);
    if (!data) return;                    /* coop_data_for_id has already said why */

    /* The game's own reset, called in its own ABI: the lifted body pops the return address
     * itself, so the caller pushes one. It preserves EBX/ESI/EBP, which is what the
     * recompiled caller of this gather expects. */
    PUSH32(0x00421243u);
    R(ECX) = d;
    fn_004061d0();

    ST32(d + 872, data);                              /* the object-data pointer */
    ST32(d + 796, LD32(data + 144));
    /* Each spawn is pushed further along x than the last, so two of them in one run do not
     * land on top of each other and read as one fighter. */
    static int spawn_n;
    const double dx = 120.0 * (double)(++spawn_n);
    ST32(d + 16, (uint32_t)((int32_t)LD32(ref + 16) + (int32_t)dx));  /* from a live fighter */
    ST32(d + 20, LD32(ref + 20));
    ST32(d + 24, LD32(ref + 24));
    STF64(d + 88,  LDF64(ref + 88) + dx);
    STF64(d + 96,  LDF64(ref + 96));
    STF64(d + 104, LDF64(ref + 104));
    ST32(d + 852, (uint32_t)dst);                     /* see the note above: imitation */
    /* The third field of LF2_COOP_SPAWN, for the portrait A/B. fn_004061d0 zeroes +0x364,
     * and a spawned fighter's HUD portrait is wrong, so this exists to test whether that
     * field is what the HUD reads -- two otherwise identical spawns with different values
     * either draw different portraits or they do not, and one run of each settles it. */
    if (sel >= 0) ST32(d + 0x364, (uint32_t)sel);
    ST8(EXISTS + (uint32_t)dst, 1);                   /* the gate fn_004064d0 tests */

    /* The watch follows the FIRST spawn of a run. With a list, a watch that silently
     * re-pointed at the last one would report on a different fighter than the reader
     * expects, so the switch is announced instead of made. */
    if (spawn_dst_idx < 0) {
        spawn_dst_obj = d; spawn_dst_idx = dst; spawn_frame = hostwin_frames();
    } else {
        fprintf(stderr, "coop spawn: the follow-up watch stays on entry %d, the first spawn "
                        "of this run -- entry %d is not being watched\n", spawn_dst_idx, dst);
    }
    fprintf(stderr, "coop spawn: built object id %d at table entry %d (%08x) from registry "
                    "data %08x at frame %ld; position from entry %d; gate byte %08x set\n",
            id, dst, d, data, spawn_frame, posref, EXISTS + (uint32_t)dst);
    if (sel >= 0)
        fprintf(stderr, "coop spawn: +0x364 forced to %d\n", sel);
}

/* Called every gather once a spawn has been attempted. Reports on a schedule AND on any
 * change back towards the idle default, because the reset is the finding. */
static void coop_spawn_watch(uint32_t self)
{
    if (spawn_dst_idx < 0) return;
    const long age = hostwin_frames() - spawn_frame;
    static int was_live = 1, said_reset;
    const int live = coop_entry_live(spawn_dst_obj);
    if (was_live && !live && !said_reset) {
        said_reset = 1;
        fprintf(stderr, "coop spawn: entry %d was RESET to the idle default %ld frames after "
                        "the clone -- the game's own sweep undid it, which is a different "
                        "answer from the spawn having no effect\n", spawn_dst_idx, age);
    }
    was_live = live;
    if (age == 1 || age == 5 || age == 30 || age == 120 || age == 300)
        fprintf(stderr, "coop spawn: +%3ld frames  entry %d %s  +000=%d char=%d hp=%d "
                        "x=%d y=%d +354=%d +418=%d\n",
                age, spawn_dst_idx, live ? "LIVE" : "idle",
                (int32_t)LD32(spawn_dst_obj + 0x000), (int32_t)LD32(spawn_dst_obj + 0x364),
                (int32_t)LD32(spawn_dst_obj + 0x2fc), (int32_t)LD32(spawn_dst_obj + 0x10),
                (int32_t)LD32(spawn_dst_obj + 0x18),  (int32_t)LD32(spawn_dst_obj + 0x354),
                (int32_t)LD32(spawn_dst_obj + 0x418));

    /* The spawned fighter draws and fights, but its HUD PORTRAIT is not its character. So
     * something reads identity from a field the spawn does not set. The shortest way to
     * that field is to diff this record against one the GAME built -- the computer opponent
     * -- once both have been running a while: what differs is what was not set, minus
     * whatever has diverged through being in different places doing different things.
     *
     * Deliberately at +90 rather than +1: at +1 the spawn's own writes dominate and every
     * position and state field differs, which buries the handful that matter. */
    /* LF2_COOP_SHOT=<n>: capture the frame n frames after the spawn, so the picture is of
     * the spawn rather than of whatever the run happened to be showing at a chosen frame. */
    {
        const char *shot = getenv("LF2_COOP_SHOT");
        if (shot && age == atol(shot)) {
            fprintf(stderr, "coop spawn: requesting a frame capture at +%ld frames\n", age);
            gfx_request_frame_dump();
        }
    }

    if (age == 90) {
        int other = -1;
        for (int k = 0; k < TABLE_N; k++)
            if (k != spawn_dst_idx && LD8(EXISTS + (uint32_t)k)) { other = k; break; }
        if (other < 0)
            fprintf(stderr, "coop spawn: no other live entry to diff against, so the "
                            "portrait question is unanswered by this run\n");
        else {
            fprintf(stderr, "coop spawn: spawned entry %d against game-built entry %d --\n"
                            "  the fields the spawn does not set are in here somewhere\n",
                    spawn_dst_idx, other);
            coop_pair_diff(self, spawn_dst_idx, other);
        }
    }
}

/* ---- LF2_COOP_REGISTRY: the object-data registry the game spawns from ----
 *
 * The spawn inlined in fn_0041bc90 reads
 *
 *     reg   = LD32(this + 2004)
 *     count = LD32(reg + 81273728)
 *     for (i = 0; i < count; i++)
 *         if (LD32(LD32(reg + 4i) + 1780) == <wanted>) break;
 *     obj->872 = LD32(reg + 4i);   obj->796 = data->144
 *
 * so the registry is an array of pointers to per-object data blocks, and every field named
 * here is one the game itself indexes by. It is dumped raw because the one thing that must
 * NOT happen is field 1780 being written down as "the character id" on the strength of one
 * comparison: fn_0041bc90 compares it against 999 and fn_004064d0 compares it against 7 and
 * 8, which are not the same kind of value. Printing the blocks side by side against entries
 * whose character is already known from the table is what settles it.
 *
 * The count's offset from the registry base is enormous (81273728 = 0x4d82000), which is a
 * real possibility for a struct this game's size but also exactly what a misread would look
 * like. So the count is sanity-checked and the dump REFUSES rather than walking an array of
 * whatever length a bad read produced. */
static void coop_registry_dump(uint32_t self)
{
    const uint32_t reg = LD32(self + REG_PTR);
    fprintf(stderr, "coop registry: frame %ld, this+%d = %08x\n",
            hostwin_frames(), REG_PTR, reg);
    if (!reg || reg < GUEST_HEAP_BASE || reg >= GUEST_HEAP_END) {
        fprintf(stderr, "coop registry: REFUSED -- %08x is not a heap pointer, so this is "
                        "not the registry and nothing was read\n", reg);
        return;
    }
    const uint32_t count_at = reg + (uint32_t)REG_COUNT_OFF;
    const int32_t count = (int32_t)LD32(count_at);
    fprintf(stderr, "coop registry: count at %08x (reg + 0x%x) reads %d\n",
            count_at, REG_COUNT_OFF, count);
    if (count <= 0 || count > 512) {
        fprintf(stderr, "coop registry: REFUSED -- a count of %d is not plausible for an "
                        "object table, so the 0x%x offset is being read wrong. Nothing was "
                        "walked; this is not an empty registry.\n", count, REG_COUNT_OFF);
        return;
    }

    /* Which registry entry each LIVE object is using, so the dump can be read against
     * characters whose identity is already known from the table. */
    fprintf(stderr, "coop registry: live objects and the data block each points at:\n");
    for (int k = 0; k < TABLE_N; k++) {
        if (!LD8(EXISTS + (uint32_t)k)) continue;
        const uint32_t o = LD32(self + PLAYER_PTRS + 4u * (uint32_t)k);
        if (!o) continue;
        const uint32_t data = LD32(o + 872);
        int which = -1;
        for (int i = 0; i < count; i++)
            if (LD32(reg + 4u * (uint32_t)i) == data) { which = i; break; }
        fprintf(stderr, "  object [%3d] char(+0x364)=%-4d data(+872)=%08x = registry[%d]\n",
                k, (int32_t)LD32(o + 0x364), data, which);
    }

    fprintf(stderr, "coop registry: %d entries -- ptr, +1780, +144, and the first printable "
                    "run in the block:\n", count);
    for (int i = 0; i < count; i++) {
        const uint32_t d = LD32(reg + 4u * (uint32_t)i);
        fprintf(stderr, "  [%3d] %08x", i, d);
        if (d < GUEST_HEAP_BASE || d >= GUEST_HEAP_END) {
            fprintf(stderr, "  NOT A HEAP POINTER -- not read\n");
            continue;
        }
        fprintf(stderr, "  +1780=%-6d +144=%-6d  \"", (int32_t)LD32(d + 1780),
                (int32_t)LD32(d + 144));
        for (uint32_t o = 0; o < 64; o++) {
            const uint8_t c = LD8(d + o);
            fputc(c >= 32 && c < 127 ? c : '.', stderr);
        }
        fprintf(stderr, "\"\n");
    }
}

static void coop_table_dump(uint32_t self)
{
    enum { MAXENT = 512 };
    const uint32_t base = LD32(self + PLAYER_PTRS);

    fprintf(stderr, "coop table: frame %ld, this=%08x, table at %08x, grid base %08x "
                    "stride 0x420\n",
            hostwin_frames(), self, self + PLAYER_PTRS, base);
    if (!base) {
        fprintf(stderr, "coop table: REFUSED -- entry 0 is null, so there is no grid base to "
                        "measure against. Not a short table; a wrong frame.\n");
        return;
    }

    int nonnull = 0, ongrid = 0, offgrid = 0, i = 0, nullrun = 0, live = 0;
    for (; i < MAXENT; i++) {
        const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
        if (!p) {
            nullrun++;
            if (nullrun >= 8) { i++; break; }
            continue;
        }
        nullrun = 0;
        nonnull++;
        const int32_t delta = (int32_t)(p - base);
        const int grid = (delta % 0x420) == 0;
        if (grid) ongrid++; else offgrid++;

        const int inheap = p >= GUEST_HEAP_BASE && p + 0x420 <= GUEST_HEAP_END;
        const int is_live = inheap && coop_entry_live(p);
        if (is_live) live++;

        /* The first eight are printed unconditionally because they are the player slots and
         * their being idle is itself the thing being measured; past that only entries that
         * are not untouched defaults are printed, or the dump is 400 identical lines. */
        if (i >= 8 && !is_live && grid && inheap && !LD8(EXISTS + (uint32_t)i)) continue;

        fprintf(stderr, "  [%3d] %08x gate=%-3d %s%s", i, p, LD8(EXISTS + (uint32_t)i),
                grid ? "" : "OFF-GRID ", is_live ? "LIVE " : "     ");
        if (grid) fprintf(stderr, "idx %-4d ", delta / 0x420);
        if (inheap)
            /* +0x338 is printed because the read profile says it is the ONLY dword the
             * per-frame sweep reads on an idle object -- 300 reads in 300 frames and
             * nothing else in the record. Whatever gates an object into the world is
             * decided from it. */
            fprintf(stderr, "+338=%-10d +000=%-4d char=%-4d hp=%-5d x=%-6d y=%-6d "
                            "+354=%-4d +418=%-4d +368=%08x",
                    (int32_t)LD32(p + 0x338),
                    (int32_t)LD32(p + 0x000),
                    (int32_t)LD32(p + 0x364), (int32_t)LD32(p + 0x2fc),
                    (int32_t)LD32(p + 0x10), (int32_t)LD32(p + 0x18),
                    (int32_t)LD32(p + 0x354), (int32_t)LD32(p + 0x418),
                    LD32(p + 0x368));
        else
            fprintf(stderr, "NOT IN THE HEAP -- not an object of this kind");
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "coop table: %d entries examined, %d non-null (%d on the 0x420 grid, %d "
                    "off it), %d LIVE; stopped %s\n",
            i, nonnull, ongrid, offgrid, live,
            i >= MAXENT ? "at the MAXENT cap, so the table may be longer than this"
                        : "after 8 consecutive nulls");
    /* What follows the 400 pointers. The per-byte read profile of `this` puts a hot dword
     * at +0x7d4 -- immediately past the table -- read about 94 times a frame during a
     * match, which is the profile of a count or a list head rather than a stored setting.
     * Printed raw, with no interpretation, because naming it before seeing it is how a
     * reading gets fixed in place. */
    fprintf(stderr, "coop table: the 64 dwords after the table (this+0x7d4 onwards):\n");
    for (int k = 0; k < 64; k += 8) {
        fprintf(stderr, "  +%03x:", 0x7d4 + k * 4);
        for (int j = 0; j < 8; j++)
            fprintf(stderr, " %11d", (int32_t)LD32(self + 0x7d4u + 4u * (uint32_t)(k + j)));
        fprintf(stderr, "\n");
    }

    if (live == 0)
        fprintf(stderr, "coop table: NOT A MATCH -- every entry is still at its initialised "
                        "default (no character, 500 HP, the origin, 99 at +0x354), so no "
                        "fighter exists on this frame. This dump says NOTHING about how "
                        "fighters are registered; the run did not reach a match.\n");
}

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

    /* LF2_COOP_REFS=<frame> -- scan memory for pointers to the player records. Outside the
     * LF2_COOP_DEBUG block on purpose: it is a one-shot scan and does not want the slot
     * table wall alongside it. */
    {
        const char *rf = getenv("LF2_COOP_REFS");
        if (rf && hostwin_frames() == atol(rf)) coop_refs_scan(self);
        /* LF2_COOP_TABLE=<frame> | live[+<n>]. The frame form is exact but brittle: the
         * data load does not take the same number of frames every run, so a scripted route
         * can be at character selection on the frame it reached the match on last time --
         * which is how the first table dump came back 400 lines of untouched defaults. The
         * `live` form fires <n> frames after the first frame on which any entry is not an
         * untouched default, so it lands in a match or does not fire at all. */
        {
            const char *tf2 = getenv("LF2_COOP_TABLE");
            static long live_at = -1, fired = -1;
            if (tf2 && *tf2) {
                if (strncmp(tf2, "live", 4) == 0) {
                    const long after = tf2[4] == '+' ? atol(tf2 + 5) : 0;
                    if (live_at < 0) {
                        const uint32_t p0 = LD32(self + PLAYER_PTRS);
                        if (p0 && coop_entry_live(p0)) live_at = hostwin_frames();
                    }
                    if (live_at >= 0 && fired < 0 && hostwin_frames() >= live_at + after) {
                        fired = hostwin_frames();
                        fprintf(stderr, "coop table: slot 0 first became live at frame %ld\n",
                                live_at);
                        coop_table_dump(self);
                        if (getenv("LF2_COOP_REGISTRY")) coop_registry_dump(self);
                        /* `auto` picks the first LIVE entry past the eight player slots --
                         * the fighter the game put in the table itself -- against its next
                         * neighbour. Which index that is varies between runs, so naming it
                         * by number would silently compare two idle records on a run where
                         * it landed elsewhere. */
                        const char *pr = getenv("LF2_COOP_PAIR");
                        int pi = -1, pj = -1;
                        if (pr && strcmp(pr, "auto") == 0) {
                            for (int k = 8; k < 400; k++) {
                                const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)k);
                                if (p && coop_entry_live(p)) { pi = k; pj = k + 1; break; }
                            }
                            if (pi < 0)
                                fprintf(stderr, "coop pair: auto found no live entry past the "
                                                "player slots, so nothing was compared\n");
                        } else if (pr) {
                            if (sscanf(pr, "%d,%d", &pi, &pj) != 2) pi = -1;
                        }
                        if (pi >= 0) coop_pair_diff(self, pi, pj);

                        /* LF2_COOP_SPAWN=<dst>[,<id>[,<+0x364>]][;...] -- a LIST, because
                         * the only way to compare two spawns fairly is to make them in the
                         * SAME run. VS mode randomises the characters, so two runs differ
                         * in the fighters already on the stage; an A/B across runs showed
                         * three portraits changing when one variable had been altered, and
                         * a difference read off that would have been the randomiser.
                         *
                         * The id defaults to 1 (Bandit), which every copy of the game has,
                         * and the spawn position comes from whichever entry is live, found
                         * here rather than assumed to be slot 0. */
                        const char *sp = getenv("LF2_COOP_SPAWN");
                        for (const char *c = sp; c && *c; ) {
                            int sd = -1, sid = 1, ssel = -1;
                            const int got = sscanf(c, "%d,%d,%d", &sd, &sid, &ssel);
                            if (got >= 1 && sd >= 0) {
                                int posref = -1;
                                for (int k = 0; k < TABLE_N; k++)
                                    if (LD8(EXISTS + (uint32_t)k)) { posref = k; break; }
                                coop_spawn(self, sd, sid, posref, ssel);
                            } else {
                                fprintf(stderr, "coop spawn: REFUSED -- each item must be "
                                                "<index>[,<object id>[,<+0x364>]], got "
                                                "\"%s\"\n", c);
                            }
                            const char *semi = strchr(c, ';');
                            c = semi ? semi + 1 : NULL;
                        }
                    }
                } else if (hostwin_frames() == atol(tf2)) {
                    coop_table_dump(self);
                }
            }
        }
    }

    coop_spawn_watch(self);

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
        /* LF2_COOP_SNAP=<a>,<b> -- slot 0's object at frame a against the same object at
         * frame b. Diffing one slot against ANOTHER slot cannot show a join, because both
         * records exist the whole time; diffing one slot across the moment it joins can.
         * The window is 0x800, twice the 0x420 stride, so a field past the stride is not
         * silently outside the picture. */
        {
            enum { SNAP_N = 0x800 / 4 };
            static uint32_t snap[SNAP_N];
            static int have;
            const char *spec = getenv("LF2_COOP_SNAP");
            long fa = 0, fb = 0;
            if (spec && sscanf(spec, "%ld,%ld", &fa, &fb) == 2) {
                const uint32_t o = LD32(self + PLAYER_PTRS);
                const long f = hostwin_frames();
                if (o && f == fa && !have) {
                    for (int k = 0; k < SNAP_N; k++) snap[k] = LD32(o + 4u * (uint32_t)k);
                    have = 1;
                    fprintf(stderr, "coop snap: slot 0 object %08x captured at frame %ld\n", o, f);
                } else if (o && f == fb) {
                    if (!have) {
                        fprintf(stderr, "coop snap: NOTHING was captured at frame %ld, so this "
                                        "diff compares against zeros -- ignore it\n", fa);
                    } else {
                        int n = 0;
                        for (int k = 0; k < SNAP_N; k++) {
                            const uint32_t v = LD32(o + 4u * (uint32_t)k);
                            if (v == snap[k]) continue;
                            if (++n <= 80)
                                fprintf(stderr, "  +%03x  before=%-11d after=%-11d "
                                                "(%08x / %08x)\n",
                                        k * 4, (int32_t)snap[k], (int32_t)v, snap[k], v);
                        }
                        fprintf(stderr, "coop snap: %d differing dwords of %d\n", n, SNAP_N);
                    }
                }
            }
        }

        /* LF2_COOP_TEST=<frame> -- set the next unset bit of the joined-players mask at that
         * frame, mid-match, and see whether a player appears. 0x00451288 was found by
         * diffing .data across a character-select join and again across a SECOND join: it
         * reads 1 with one player and 3 with two, which is a per-player bitmask and not a
         * count. Whether flipping it mid-match is enough -- whether a fighter follows -- is
         * exactly what this answers, and it is the whole question for drop-in. */
        {
            const char *tf = getenv("LF2_COOP_TEST");
            if (tf && hostwin_frames() == atol(tf)) {
                const uint32_t m = LD32(JOINED_MASK);
                int bit = 0;
                while (bit < 8 && (m & (1u << bit))) bit++;
                if (bit < 8) {
                    /* The mask alone does nothing mid-match -- measured -- because it is
                     * read when the match STARTS. So this also gives the idle slot the state
                     * a playing one has, by copying the playing record over it. Two fields
                     * are kept: +368, which differs per slot and looks like the slot's own
                     * buffer, and the x position, so the new player does not land exactly on
                     * top of the one it was copied from.
                     *
                     * RESULT: not sufficient. The mask is set and the record is complete and
                     * still no third fighter appears or draws. So being a filled-in player
                     * record is not what puts a fighter in the world -- there is a list of
                     * active objects it also has to be in, and finding that is the next
                     * step. Kept as the probe that established it. */
                    const uint32_t src = LD32(self + PLAYER_PTRS);
                    const uint32_t dst = LD32(self + PLAYER_PTRS + 4u * (uint32_t)bit);
                    if (src && dst) {
                        const uint32_t keep368 = LD32(dst + 0x368);
                        for (uint32_t o = 0; o < 0x420u; o += 4)
                            ST32(dst + o, LD32(src + o));
                        ST32(dst + 0x368, keep368);
                        ST32(dst + 0x10, LD32(src + 0x10) + 120u);   /* x, in ints   */
                        ST32(dst + 0x5c, LD32(src + 0x5c) + 0x400u); /* x, the float */
                    }
                    ST32(JOINED_MASK, m | (1u << bit));
                    fprintf(stderr, "coop test: joined mask %08x -> %08x (set bit %d), "
                                    "record %08x cloned from %08x\n",
                            m, m | (1u << bit), bit, dst, src);
                } else {
                    fprintf(stderr, "coop test: mask %08x is already full, nothing set\n", m);
                }
            }
        }

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
