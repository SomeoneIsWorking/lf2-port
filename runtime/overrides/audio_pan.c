/* fn_00416fb0 / fn_00417090 -- how loud a sound is in each ear, from where it is on screen.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 *
 * WHY THIS FILE EXISTS: widescreen was silent on the right (issue #39). Not quieter -- SILENT,
 * for the right 48% of a 1920-wide view, and it had been that way since the composition
 * started following the window.
 *
 * WHAT THE GAME DOES, read off the 211 bytes of fn_00416fb0 rather than guessed. It is a
 * two-speaker pan, and the speakers are placed on the SCREEN:
 *
 *     sx = world_x - camera                       // where the sound is on screen
 *     left  = falloff(|sx - 200|)                 // a speaker at screen x 200
 *     right = falloff(|sx - 600|)                 // and one at 600
 *     falloff(d) = 100                if d < 200
 *                  (400 - d) * 100 / 200   if d < 400
 *                  0                otherwise
 *     accum_left[i]  += left;  accum_right[i] += right;  seen[i] = 1
 *
 * -- with the first write to a slot clearing it first, so the two accumulate across every
 * source of the same sound in a frame. The magic multiply by 0x51eb851f and SAR 6 is a
 * divide by 200; the SHR 31 / ADD after it is the usual sign correction, and the numerator
 * is never negative here, so it is plain integer division.
 *
 * THE FOUR CONSTANTS ARE THE 794 SCREEN, WRITTEN DOWN AS PIXELS. 200 and 600 are its quarter
 * points (794/4 = 198, 3*794/4 = 595) and the radii are a quarter and a half of it. So the
 * audible span is sx in (-200, 1000): wider than the game's own screen, which is why at 794
 * nothing is ever culled and nobody had reason to look at this function.
 *
 * Widen the view and the span does not move. At 1920 a sound past screen x 1000 is outside
 * BOTH speakers' reach and its volume is exactly zero -- the game culling audio against a
 * screen the port stopped using.
 *
 * THE PORT: the same function with the constants scaled by view/794. That is a scale rather
 * than a re-derivation on purpose -- deriving them from the view as `view/4` and `3*view/4`
 * would give 198 and 595 at the native width instead of the 200 and 600 the game shipped, and
 * change a game nobody asked to change. At view == 794 every constant is bit-for-bit what it
 * was, which is the property `ctest audio_pan` checks.
 *
 * ABI: cdecl, RET 0 -- the caller pops. Two arguments (world x, slot index). At return the
 * original leaves the right-ear volume in EAX and &accum_right[i] in ECX; both are reproduced
 * because the callers are recompiled code and this cannot see which of them it reads.
 */

#include "overrides.h"
#include "world.h"

#include "../guest_ops.h"

#include <stdio.h>
#include <stdlib.h>

/* The game's own numbers, in the units it wrote them: pixels of a 794-wide screen. */
enum { PAN_LEFT_X = 200, PAN_RIGHT_X = 600, PAN_NEAR = 200, PAN_FAR = 400, PAN_FULL = 100 };

/* The two slot tables each function accumulates into, and the flag that says a slot has been
 * touched this frame. Two identical routines with two sets of tables -- the game keeps two
 * categories of sound and pans them the same way. */
enum { PAN_A_SEEN = 0x00457588, PAN_A_L = 0x00457bc8, PAN_A_R = 0x00452170 };
enum { PAN_B_SEEN = 0x00453e10, PAN_B_L = 0x004527e8, PAN_B_R = 0x004554c8 };

static long pan_calls, pan_silent;

/* A screen constant, scaled to the view actually being drawn. Exact at the game's own width:
 * 200 * 794 / 794 is 200, not 199. */
static int pan_scaled(int px)
{
    /* LF2_AUDIO_PAN_RAW=1 keeps the game's screen constants unscaled, which is the behaviour
     * this file replaced. It is the NEGATIVE ARM of tools/audio_pan_test.sh: "the audible
     * span covers the picture" would pass just as happily on a build where the span was
     * always enormous, so the test also has to show that WITHOUT the scaling the span fails
     * to cover a wide view. Never set in normal use. Read once -- this runs per sound. */
    static int raw = -1;
    if (raw < 0) raw = getenv("LF2_AUDIO_PAN_RAW") != NULL;
    if (raw) return px;
    const long view = bg_view_width();
    return (int)(((long)px * view) / (long)BG_SCREEN_W);
}

static int pan_falloff(int32_t sx, int centre, int near, int far)
{
    int d = (int)(sx - centre);
    if (d < 0) d = -d;
    if (d < near) return PAN_FULL;
    if (d >= far) return 0;
    /* (far - d) * 100 / (far - near). At the native width that is the game's own
     * (400 - d) * 100 / 200, including the truncation -- the numerator cannot be negative
     * inside this branch, so there is no rounding difference to reproduce. */
    return ((far - d) * PAN_FULL) / (far - near);
}

static void pan_apply(uint32_t seen_tab, uint32_t l_tab, uint32_t r_tab)
{
    const int32_t world_x = (int32_t)LD32(R(ESP) + 4);
    const uint32_t slot   = LD32(R(ESP) + 8);
    const int32_t sx = world_x - (int32_t)LD32(BG_CAMERA_X);

    const int near = pan_scaled(PAN_NEAR), far = pan_scaled(PAN_FAR);
    const int vl = pan_falloff(sx, pan_scaled(PAN_LEFT_X),  near, far);
    const int vr = pan_falloff(sx, pan_scaled(PAN_RIGHT_X), near, far);

    pan_calls++;
    if (!vl && !vr) pan_silent++;

    const uint32_t off = slot * 4u;
    /* The game clears both accumulators the first time a slot is touched in a frame, so they
     * sum the sources of one sound rather than growing without bound. */
    if (LD32(seen_tab + off) == 0) { ST32(l_tab + off, 0); ST32(r_tab + off, 0); }
    ST32(l_tab + off, LD32(l_tab + off) + (uint32_t)vl);
    ST32(r_tab + off, LD32(r_tab + off) + (uint32_t)vr);
    ST32(seen_tab + off, 1);

    R(EAX) = (uint32_t)vr;
    R(ECX) = r_tab + off;
    R(ESP) += 4;                       /* cdecl: pop the return address only */
}

void fn_00416fb0(void) { pan_apply(PAN_A_SEEN, PAN_A_L, PAN_A_R); }
void fn_00417090(void) { pan_apply(PAN_B_SEEN, PAN_B_L, PAN_B_R); }

/* WHERE A SOUND CAN BE HEARD AT ALL, against the view being drawn.
 *
 * A pan is hard to see in a screenshot and impossible to hear in a headless run, so the thing
 * to report is the SPAN: the range of screen x with any volume in either ear, beside the width
 * of the picture. If the span does not cover the picture, sounds in the uncovered part are
 * culled -- which is the whole of issue #39's audio half, and it is a number rather than an
 * opinion.
 *
 * It prints the game's own numbers too, so the scaling can be checked by eye: at 794 the span
 * must be exactly (-200, 1000), because that is what the shipped constants give. */
void audio_pan_report(void)
{
    if (!getenv("LF2_AUDIO_PAN")) return;
    const int view = bg_view_width();
    const int far = pan_scaled(PAN_FAR);
    const int lo = pan_scaled(PAN_LEFT_X) - far + 1;
    const int hi = pan_scaled(PAN_RIGHT_X) + far - 1;
    fprintf(stderr, "audio pan: view %d -- speakers at %d and %d, audible screen x %d..%d\n",
            view, pan_scaled(PAN_LEFT_X), pan_scaled(PAN_RIGHT_X), lo, hi);
    if (lo <= 0 && hi >= view - 1)
        fprintf(stderr, "audio pan: the audible span covers the whole %d-wide picture, so "
                        "nothing on screen is culled\n", view);
    else
        fprintf(stderr, "audio pan: the audible span does NOT cover the picture -- screen x "
                        "%d..%d of %d is SILENT, which is issue #39's audio half\n",
                hi + 1 < view ? hi + 1 : 0, view - 1, view);
    fprintf(stderr, "audio pan: %ld pan(s) computed, %ld of them fully silent%s\n",
            pan_calls, pan_silent,
            pan_calls ? "" : " -- NO sound was panned in this run, so these numbers describe "
                             "nothing");
}
