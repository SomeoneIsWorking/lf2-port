/* The native overrides, as one subsystem: what its files share with each other.
 *
 * Each .c here provides the fn_<addr> symbols the lifter would otherwise have generated,
 * for the addresses listed in re/overrides.txt. They run in the guest ABI: the caller's
 * arguments are on the guest stack, and a stdcall callee pops them.
 *
 * The split is by what the code is ABOUT, not by which recompiled function it happens to
 * replace -- one screen's behaviour is usually spread over several overrides, and one
 * override (fn_0043f010 draws everything) serves several screens:
 *
 *   menu.c        the front end: its selection, its mouse and pad, the advertising panel,
 *                 and the widescreen geometry the whole port hangs off
 *   screens.c     the post-load screens' mouse -- mode menu, character select, overlay
 *   input.c       the per-frame gather: devices to the seven buttons a fighter acts on
 *   coop.c        the object world: building a fighter, joining, leaving, choosing one
 *   coop_debug.c  the instruments over that world -- every LF2_COOP_* probe, and the
 *                 follow-up watch the spawn diagnostics report through
 *   hud.c         the in-match HUD strip: the one pass that may see a player slot the
 *                 stage may not, which is how a drop-in chooses a character without
 *                 standing in the fight
 *   text.c        the clip and glyph draw hooks
 *   assets.c      the data-file decrypt
 *
 * The line between coop.c and coop_debug.c is the one worth keeping: coop.c is what the
 * game does, coop_debug.c is how this port knows it did it. A probe that grows into a
 * mechanism moves across; a mechanism that only exists to be measured never should have
 * been in coop.c.
 */

/* Which recompiled function each file provides, so a reader looking for one address does
 * not have to open seven files:
 *
 *   fn_004246b0  menu.c     the front-end menu -- selection index, so a pad can drive it
 *   fn_00423b00  menu.c     element draw -- declines the advertising panel by descriptor
 *   fn_00419a60  input.c    per-frame player input -- devices into the game's buttons
 *   fn_0041ae60  hud.c      the in-match HUD strip -- one panel per player slot
 *   fn_0043f010  text.c     clip draw -- glyph hinting, the ad notice, the game's cursor
 *   fn_00423940  text.c     font sheet selection
 *   fn_0043c4a0  text.c     text draw
 *   fn_004148a0  assets.c   the data-file decrypt
 *
 * charselect_mouse / modemenu_mouse / overlay_mouse in screens.c are not overrides of their
 * own -- they are called from the ones above, because the screens they drive have no
 * function boundary this port can replace.
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
#ifndef LF2_OVERRIDES_H
#define LF2_OVERRIDES_H

#include <stdint.h>

/* The game's top-level mode, as the front-end menu sets it every frame. 2 covers BOTH
 * character selection and the match, which is why "is a match on screen" has to be asked of
 * panel_hud_up() rather than of this. */
enum { MODE_ENTER = 1, MODE_IN_GAME = 2 };
uint32_t game_top_mode(void);          /* menu.c owns the storage */

/* Set when a mouse click on a ported screen should read as the keyboard device's ATTACK for
 * a gather or two, so the game does its own dispatch, sound and screen change rather than
 * the port simulating them. screens.c and menu.c set it; input.c spends it. */
extern int mouse_confirm_frames;

/* The ported mouse handling for the post-load screens (screens.c). */
void charselect_mouse(void);
void modemenu_mouse(void);
void overlay_mouse(void);
int  overlay_open(void);

/* The widescreen width in force, or 0 for the game's own 794 (menu.c). */
int  lf2_wide_width(void);


/* ---- the game's own .data, where more than one of these files reads it ----
 *
 * An address used by a single file stays in that file; these are here because the screens,
 * the draw hooks and the menu all read the same words, and one copy per file is one copy
 * per file to get wrong.
 */

/* Mouse position, as the menu itself reads it. */
enum { GX_MOUSE_X = 0x004546f0, GX_MOUSE_Y = 0x00453cdc };

/* The pre-fight overlay on the character-select screen: Fight! / Reset All / Reset Random
 * / Background / Difficulty / Exit, index 0..5, up decrements and wraps. Located by
 * diffing .data across a single d-pad press rather than by reading fn_0041bc90, where a
 * search for the compare returned 20+ indistinguishable candidates; confirmed by matching
 * 2 -> 1 against the frame where the highlight moved Reset Random -> Reset All. */
enum { OVERLAY_SEL = 0x0044d06c };

/* The ad system's update notice in the top-right corner; declined in text.c. */
enum { MENU_CLIP7 = 0x00451188 };            /* sheet handle, loaded from "MENU_CLIP7" */
enum { CURSOR_SHEET = 0x00451170 };          /* sheet handle of the game's own mouse cursor */
enum { NOTICE_X = 725, NOTICE_Y = 5 };       /* the game's own constants for the notice */

/* input.c: which player the keyboard claimed, or -1. The mouse drives the same one, so the
 * two never disagree about who they are. */
int  keyboard_player(void);

/* runtime/win32.c */
int  hostwin_pointer(int *x, int *y);
int  hostwin_mouse_clicked(void);      /* one-shot, per press */

#endif
