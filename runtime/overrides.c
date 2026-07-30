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

/* The pre-fight overlay on the character-select screen: Fight! / Reset All / Reset Random
 * / Background / Difficulty / Exit, index 0..5, up decrements and wraps. Located by
 * diffing .data across a single d-pad press rather than by reading fn_0041bc90, where a
 * search for the compare returned 20+ indistinguishable candidates; confirmed by matching
 * 2 -> 1 against the frame where the highlight moved Reset Random -> Reset All. */
enum { OVERLAY_SEL = 0x0044d06c };

/* The ad system's update notice in the top-right corner; see fn_0043f010 below. */
enum { MENU_CLIP7 = 0x00451188 };            /* sheet handle, loaded from "MENU_CLIP7" */
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
    /* The update notice in the top-right corner is gone (see fn_0043f010), so its hit box
     * must go with it, or the menu keeps an invisible control that opens sub-screen -3. The
     * game's own test is `mouse.x >= 725 && mouse.y < 18` with no upper bound on x or lower
     * bound on y -- the whole corner. Swallowing the click here rather than letting the
     * original body act on it is the port owning a control it removed; nothing else in the
     * menu is hit-tested in that region. */
    if (LD32(GX_CLICK) && (int32_t)LD32(GX_MOUSE_X) >= NOTICE_X
                       && (int32_t)LD32(GX_MOUSE_Y) < 18)
        ST32(GX_CLICK, 0);

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

void fn_00419a60(void)
{
    const uint32_t self = R(ECX);              /* __thiscall */
    const uint32_t mask_buf = LD32(R(ESP) + 12);

    fn_00419a60__orig();                       /* configured devices, unchanged */

    const int want_mask   = LD32(NET_OR_RECORD) != 0;
    const int want_mirror = (int8_t)LD8(RECORDING) > 0;

    /* Counters, not a hit log: the interesting failure is that this never merges anything,
     * and a diagnostic that only prints when it does would be silent in exactly that case.
     * live/pads/merges together say which of "no player slot", "no controller" and "nothing
     * pressed" actually happened. */
    in_frames++;
    /* Pads are handed to live slots in order, so pad 0 is the first joined player and
     * pad 1 the second. A slot the game has filled with a computer is still a live slot,
     * which sounds like a problem and is not: an unjoined slot shows "Join?" until player
     * one PROCEEDS past character selection, so a second pad that presses before then owns
     * the slot itself. Measured, after this was documented wrongly twice -- see
     * docs/running.md and tools/controller_2p_test.sh.
     */
    int pad_index = 0;
    for (uint32_t sel = DEVSEL, i = 0; sel < DEVSEL_END; sel += 4, i++) {
        if ((int32_t)LD32(sel) <= 0) continue;         /* slot takes no live input */
        in_live++;

        unsigned char btn[7];
        if (!gamepad_player_buttons(pad_index++, btn)) continue;
        in_padded++;

        const uint32_t obj = LD32(self + PLAYER_PTRS + 4 * i);
        if (!obj) continue;
        for (int b = 0; b < 7; b++) in_presses += btn[b];

        for (int b = 0; b < 7; b++) {
            if (!btn[b]) continue;
            ST8(obj + BTN_CUR + b, 1);
            if (want_mask)
                ST8(mask_buf + i, (uint8_t)(LD8(mask_buf + i) | BTN_BIT[b]));
            if (want_mirror)
                ST8(MASK_MIRROR + i, (uint8_t)(LD8(MASK_MIRROR + i) | BTN_BIT[b]));
        }
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
