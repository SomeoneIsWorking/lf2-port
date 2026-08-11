/* fn_0043f010 / fn_00423940 / fn_0043c4a0 -- clip and glyph drawing.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "world.h"

#include "guest_ops.h"
#include "guest_map.h"
#include "hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fn_0043f010__orig(void);
void fn_00423940__orig(void);

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
    /* THE STAGE'S OWN SHADOW. The game draws a flat dithered ellipse under every object from
     * a per-stage bitmap (bg.dat's `shadow:` / `shadowsize:`). Which OBJECT that draw is made
     * on is what identifies it, and it is learned rather than assumed: clip_obj_note hands
     * the blit path the object of the draw in progress, and the blit path -- which is the
     * only place that knows the rectangle -- reports back which object drew the ellipse.
     *
     * An earlier attempt read a pointer out of the background record at -1128, next to the
     * shadowsize. It was wrong: 40000 clip draws with a stage loaded, ZERO on that pointer.
     * It belongs to the neighbouring background's record, the records being contiguous. */
    clip_obj_note(R(ECX));
    const uint32_t shadow_obj = shadow_object();
    shadow_hint_set(shadow_obj && R(ECX) == shadow_obj);

    const int sheet = font_sheet_index(R(ECX));
    if (sheet >= 0) {
        /* LF2_GLYPH_POS=1: every sheet glyph with the position the GAME asked for, before any
         * of the port's offsets. The fighters' name tags are drawn this way -- not through
         * TextOutA and not through fn_00423940, both eliminated -- so this is the only place
         * their x can be read rather than inferred from a screenshot (issue #55). */
        if (getenv("LF2_GLYPH_POS"))
            fprintf(stderr, "glyph sheet=%d x=%d y=%d ch=%d\n", sheet,
                    (int32_t)LD32(R(ESP) + 4), (int32_t)LD32(R(ESP) + 8),
                    (int32_t)LD32(R(ESP) + 12));
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
