/* fn_0043f010 / fn_00423940 / fn_0043c4a0 -- clip and glyph drawing.
 *
 * One of the hand-written native replacements for guest routines; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "environment.h"
#include "overrides.h"
#include "boot_guest.h"
#include "world.h"
#include "guest_cursor.h"
#include "geom.h"

#include "guest.h"
#include "guest_map.h"
#include "hostwin.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * A broader native replacement would require recovering fn_004246b0 around
 * this block. That larger behavior owner is not required to remove one label.
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
    /* Startup owns data loading as one blocking operation, not as a rendered screen. The
     * original one-time initializer mixes progress art into the work and reaches this draw
     * before its loading-picture sheet exists when mode 1 is bypassed. Decline every such
     * draw at the guest boundary; the data constructors and file loaders do not depend on
     * pixels produced here. */
    if (boot_guest_loading_data()) {
        R(ESP) += 4 + 24;
        return;
    }

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
        /* The camera goes on the SAME line on purpose. A tag x that does not change with the
         * view proves nothing unless the camera actually left the stage's left edge that
         * frame: below the clamp the shifted and unshifted cameras are equal, so a run whose
         * fighter never walks cannot tell the two hypotheses apart -- which is exactly how an
         * earlier "identical at both widths" reading was taken for an answer. cam is the
         * game's own camera, draw is what bg_draw_camera returns; they differ only when the
         * widescreen shift is actually biting. */
        if (lf2_environment_get(LF2_ENV_GLYPH_POS))
            lf2_log_writef(LF2_LOG_INFO, "text", "glyph sheet=%d x=%d y=%d ch=%d ret=%08x cam=%d draw=%d\n", sheet,
                           (int32_t)LD32(R(ESP) + 4), (int32_t)LD32(R(ESP) + 8), (int32_t)LD32(R(ESP) + 12),
                           LD32(R(ESP)), (int32_t)LD32(BG_CAMERA_X),
                           geom_draw_camera((int32_t)LD32(BG_CAMERA_X), bg_view_width()));
        glyph_hint_set((int32_t)LD32(R(ESP) + 12));
        lf2_jit_call_original(0x0043f010);
        glyph_hint_clear();
        return;
    }

    if (R(ECX) == LD32(MENU_CLIP7) && LD32(R(ESP) + 4) == NOTICE_X && LD32(R(ESP) + 8) == NOTICE_Y) {
        R(ESP) += 4 + 24; /* RET 0x18: return address and six args */
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
     * The old test also required `(draw x,y) == (mouse x,mouse.y+2)`. That was layout
     * coincidence, not identity, and a widescreen transform could let the cursor through.
     * The exact three producer calls are stable and guest_cursor_draw tests both them and
     * the shared sheet, so the menu artwork on that sheet remains. */
    if (guest_cursor_draw(LD32(R(ESP)), R(ECX), LD32(CURSOR_SHEET))) {
        R(ESP) += 4 + 24; /* RET 0x18: return address and six args */
        return;
    }

    /* Which call draws the mouse cursor? It reaches Blt as an 11x19 sprite at the
     * pointer, but that blit is issued from inside this function, which draws everything
     * -- so the identity has to come from this call's own arguments and caller. */
    if (lf2_environment_get(LF2_ENV_CURSOR_TRACE)) {
        const int ax = (int)LD32(R(ESP) + 4), ay = (int)LD32(R(ESP) + 8);
        const int mx = (int)LD32(GX_MOUSE_X), my = (int)LD32(GX_MOUSE_Y);
        if (ax >= mx - 4 && ax <= mx + 4 && ay >= my - 4 && ay <= my + 4) {
            static uint32_t seen[8];
            static int n;
            const uint32_t ra = LD32(R(ESP));
            int known = 0;
            for (int i = 0; i < n; i++)
                if (seen[i] == ra) {
                    known = 1;
                    break;
                }
            if (!known && n < 8) {
                seen[n++] = ra;
                lf2_log_writef(LF2_LOG_INFO, "text",
                               "cursor draw: caller=%08x args x=%d y=%d clip=%d sheet=%08x "
                               "(pointer %d,%d)\n",
                               ra, ax, ay, (int32_t)LD32(R(ESP) + 12), R(ECX), mx, my);
                /* The handle is a heap pointer with no stable identity across runs; the
                 * .data slot that HOLDS it does have one. Find it. */
                for (uint32_t a = 0x0044d000; a < 0x00459724; a += 4)
                    if (LD32(a) == R(ECX))
                        lf2_log_writef(LF2_LOG_INFO, "text", "    sheet handle also lives at .data %08x\n", a);
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
    lf2_jit_call_original(0x0043f010);
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
    if (lf2_environment_get(LF2_ENV_GAMETEXT_DEBUG)) {
        char buf[128];
        const uint32_t str = LD32(R(ESP) + 4);
        unsigned n = 0;
        for (; n < sizeof buf - 1; n++) {
            const uint8_t c = LD8(str + n);
            if (!c) break;
            buf[n] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        buf[n] = 0;
        lf2_log_writef(LF2_LOG_INFO, "text", "gametext x=%d y=%d cols=%d rows=%d font=%d \"%s\"\n",
                       (int32_t)LD32(R(ESP) + 8), (int32_t)LD32(R(ESP) + 12), (int32_t)LD32(R(ESP) + 16),
                       (int32_t)LD32(R(ESP) + 20), (int32_t)LD32(R(ESP) + 24), buf);
    }
    lf2_jit_call_original(0x00423940);
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
    if (lf2_environment_get(LF2_ENV_ADS_ON)) {
        lf2_jit_call_original(0x0043c4a0);
        return;
    }
    R(EAX) = 0;
    R(ESP) += 4; /* pop the return address only */
}

/* ---------------------------------------------------------------------------
 * fn_0041b130 -- the mode caption at the bottom of the screen, "Stage mode (Difficult)".
 *
 * Issue #60. It is drawn RIGHT-ANCHORED to the game's own 794-wide screen and nothing moved
 * it when the composition is wider, so on a 978-wide view it sat 184 px short of the edge.
 *
 * READ OUT OF THE DECOMPILATION, and the entry's previous conclusion was wrong because it was
 * not. That note searched the two small callers of the outline wrapper for the literals 0x31a
 * and 0x319, found neither, and concluded the constant must live in fn_004246b0 -- one of the
 * four monoliths this project does not hand-port -- which closed the issue off as "decide
 * whether a static label is worth a data-word hunt inside 20 KB". Neither half held up. The
 * constant is 0x316, not 0x31a, and the x is not a literal at all:
 *
 *     FUN_00423a70(&caption, strlen(caption) * -8 + 0x316, 0x213, 0x40, 4, 0, 0)
 *
 * 0x316 is 790, which is 794 less a four-pixel right margin, and the layout is the standard
 * right-anchor: the string's own length decides where it starts. That is in fn_0041b130, 598
 * bytes, not in any monolith -- so the substitution that fixed issues #55 and #58 applies
 * here after all: own the function, and make its 794 the view.
 *
 * WHAT THIS FUNCTION DOES, in full: it assembles the caption into the guest buffer at
 * CAPTION from the game mode (its first argument) and the difficulty word, then draws it. The
 * strings are copied FROM THEIR GUEST ADDRESSES rather than transcribed into this file --
 * they are the shipped binary's text and do not belong in the repo, and reading them at
 * runtime is also what makes this port exact rather than a re-typing of it.
 *
 * The two y values are the game's: 531 normally, 510 when the caller asks for the raised
 * position. Neither moves, because the vertical layout is not what widescreen changes.
 *
 * WHY THE PORT'S CONTROLS HINT DOES NOT COLLIDE, which the entry flagged as the open risk:
 * that hint is LEFT-anchored (`8 + screen_offset_x()`, runtime/video/ddraw.c) on the same
 * bottom edge. Right-anchoring the caption to the VIEW moves it further from the hint than it
 * is today, not closer -- the two only converge as the view NARROWS, and the view floors at
 * the game's own 794 where this is byte-identical anyway.
 *
 * ABI: __cdecl-in-guest, two stack arguments, RET 8.
 * ------------------------------------------------------------------------ */

enum { CAPTION = 0x00450c38, DIFFICULTY = 0x00450c30, STAGE_KIND = 0x00450b94 };
/* The right margin the game lays the caption out against: 794 - 790. */
enum { CAPTION_MARGIN = GEOM_SCREEN_W - 0x316, CAPTION_Y = 0x213, CAPTION_Y_RAISED = 0x1fe };

/* The game's own strings, by guest address. Nothing here is a copy of one. */
static uint32_t caption_mode_str(int32_t mode)
{
    switch (mode) {
    case 0: return 0x004490cc;
    case 1: return ((int32_t)LD32(STAGE_KIND) / 10 == 5) ? 0x004490b0 : 0x004490c0;
    case 2: return 0x0044909c;
    case 3: return 0x00449094;
    case 4: return 0x00449084;
    default: return 0; /* the game leaves the buffer alone; so does this */
    }
}

static uint32_t caption_difficulty_str(void)
{
    switch ((int32_t)LD32(DIFFICULTY)) {
    case 0: return 0x004490a4;
    case 1: return 0x00449078;
    case 2: return 0x00449070;
    case -1: return 0x00449064;
    default: return 0; /* no suffix, exactly as the game's fall-through */
    }
}

static uint32_t guest_append(uint32_t dst, uint32_t src)
{
    if (!src) return dst;
    while (LD8(dst)) dst++;
    for (;;) {
        const uint8_t c = LD8(src++);
        ST8(dst++, c);
        if (!c) break;
    }
    return dst - 1; /* the new terminator */
}

void fn_0041b130(void)
{
    const int32_t mode = (int32_t)LD32(R(ESP) + 4);
    const int32_t raised = (int32_t)LD32(R(ESP) + 8);

    const uint32_t base = caption_mode_str(mode);
    if (base) {
        ST8(CAPTION, 0);
        guest_append(CAPTION, base);
    }
    const uint32_t end = guest_append(CAPTION, caption_difficulty_str());
    const int len = (int)(end - CAPTION);

    /* THE ONE CHANGED NUMBER. The game anchors to its own screen's right edge; the port
     * anchors to the VIEW's, which is the same thing at 794 and is what widescreen means
     * everywhere else in this port. */
    const int32_t x = (int32_t)bg_view_width() - CAPTION_MARGIN - 8 * len;
    const int32_t y = raised ? CAPTION_Y_RAISED : CAPTION_Y;

    /* fn_00423a70(str, x, y, cols, rows, 0, 0) -- the outline wrapper, seven arguments pushed
     * right to left, __cdecl: the callee pops only the return address and this pops the
     * arguments. The return address is the game's own, from the call site this replaces
     * (guest 0x0041b349), so a trace through here reads as the original call rather than as a
     * synthetic one. */
    PUSH32(0u);
    PUSH32(0u);
    PUSH32(4u);
    PUSH32(0x40u);
    PUSH32((uint32_t)y);
    PUSH32((uint32_t)x);
    PUSH32(CAPTION);
    PUSH32(0x0041b349u);
    lf2_jit_call(0x00423a70);
    R(ESP) += 28;

    R(ESP) += 4 + 8; /* RET 8 */
}
