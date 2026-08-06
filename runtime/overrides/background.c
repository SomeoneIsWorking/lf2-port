/* fn_0041a250 -- the stage's background layer draw.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 *
 * WHY THIS IS AN OVERRIDE AND NOT A BLIT-PATH HEURISTIC. Widescreen has to decide, for every
 * background layer, whether there is more picture to show beside the game's 794 and at what
 * step to repeat it. The blit stream cannot answer that: a layer that repeats and a layer
 * that does not look identical by the time they reach Blt, and runtime/ddraw.c's contiguity
 * rule -- "this copy starts exactly where the last one ended, so continue the run" -- guessed
 * wrong on Brokeback Clif and repeated the middle cliff across the widened band with a hard
 * seam at each end. The layer itself says which it is; this is the function that reads it.
 *
 * WHAT THE GAME'S OWN PASS DOES, read straight off fn_0041a250 (828 bytes, every instruction
 * accounted for) rather than inferred:
 *
 *     if (background index == 99)  ->  fn_0041a050, a different pass entirely
 *     for (i = 0; i < layer_count; i++) {
 *         if (tint[i])  { colour-fill this layer through fn_00415160; continue; }
 *         off = (stage_width > 794)
 *                 ? -((span[i] - 794) * camera) / (stage_width - 794)
 *                 : 0;
 *         if (cc[i] > 0) {                       // an animated layer
 *             anim[i] = (anim[i] + 1) % cc[i];   // stepped whether or not it draws
 *             if (anim[i] < c1[i] || anim[i] > c2[i]) continue;
 *         }
 *         if (loop[i])  for (x = x[i]; x < span[i]; x += loop[i])  draw(off + x, y[i]);
 *         else          draw(off + x[i], y[i]);
 *     }
 *
 * THE TWO FIELDS THAT MATTER, and the distinction the port kept getting wrong:
 *
 *   span (bg.dat `width:`)  is NOT a repeat period. It is how far the layer scrolls, and the
 *                           bound of its tiling. Every stage's layers are authored so that
 *                           span - 794 is exactly the scroll range, which makes the layer
 *                           cover the 794 screen at EVERY camera position with no margin.
 *   loop (bg.dat `loop:`)   is the repeat step, and 0 means the layer is drawn once.
 *
 * The 794 appears ONLY in the parallax. It is not a loop bound -- ddraw.c's comment said the
 * layer count came from "an immediate 794 inside FUN_0041a250" and that was simply not so.
 *
 * WHAT FOLLOWS FROM IT for widescreen (issues #23 and #28, and claim C017): a non-looping
 * layer has 794 pixels of picture and never any more, at any camera, on any stage -- and
 * every stage's sky is non-looping. So there is nothing to uncover beside it; only a LOOPING
 * layer can be carried further, and then at its own declared step rather than a guessed one.
 *
 * Verified against the body it replaces by drawing the same frames both ways: see
 * tools/background_test.sh.
 */

#include "overrides.h"
#include "world.h"

#include "../com.h"
#include "../guest_ops.h"

#include <stdio.h>
#include <stdlib.h>

void fn_0041a250__orig(void);

enum { BG_ALT_PASS = 99 };              /* the index fn_0041a250 hands to fn_0041a050 */
enum { DRAW_CLIP = 0x0043f010,          /* six args, stdcall: the callee pops them */
       DRAW_FILL = 0x00415160 };        /* five args, cdecl:  this function pops them */

/* A layer field, addressed the way the game addresses it. Kept local to the draw so the
 * per-layer reads read like the disassembly they came from. */
static uint32_t lf(uint32_t registry, uint32_t bg, uint32_t field, int i)
{
    return LD32(registry + (bg * BG_STRIDE_DW + (uint32_t)i) * 4u + field);
}

static void lf_store(uint32_t registry, uint32_t bg, uint32_t field, int i, uint32_t v)
{
    ST32(registry + (bg * BG_STRIDE_DW + (uint32_t)i) * 4u + field, v);
}

/* One picture, at one place. The clip index is -1 -- the whole picture, not a glyph. */
static void draw_layer(uint32_t obj, int32_t x, int32_t y, uint32_t pic, uint32_t arg0)
{
    const uint32_t args[6] = { (uint32_t)x, (uint32_t)y, 0xffffffffu, pic, 0u, arg0 };
    R(ECX) = obj;
    guest_call(DRAW_CLIP, args, 6);
}

/* A layer with a tint is a colour fill rather than a picture, and the game remaps four
 * specific colours before filling. The four pairs are its own constants, copied exactly:
 * a table of "this colour becomes that one" with no pattern to generalise from, so writing
 * them as anything other than what they are would be inventing a rule the game does not have.
 */
static uint32_t tint_remap(uint32_t c)
{
    switch (c) {
    case 0x175317u: return 0x104f10u;
    case 0x575347u: return 0x5a4e4bu;
    case 0x977757u: return 0x9a6e5au;
    case 0x473f1fu: return 0x423818u;
    default:        return c;
    }
}

static void fill_layer(uint32_t registry, uint32_t bg, int i, uint32_t tint)
{
    const uint32_t args[5] = {
        lf(registry, bg, BG_LAYER_X, i),
        lf(registry, bg, BG_LAYER_Y, i),
        lf(registry, bg, BG_LAYER_SPAN, i),
        lf(registry, bg, BG_LAYER_HEIGHT, i),
        tint_remap(tint),
    };
    guest_call(DRAW_FILL, args, 5);
    R(ESP) += 5 * 4;                     /* cdecl: fn_00415160 pops only its return address */
}

/* The parallax, in the game's own order of operations: the product first, then the divide,
 * then the negate. Rearranging it would change the rounding -- x86 IDIV and C both truncate
 * toward zero, so `-((span - w) * camera) / (stage - w)` and `-(span - w) * (camera / ...)`
 * are different pictures, one pixel at a time.
 *
 * The game guards this divide only on the non-looping path; on the looping path it divides
 * unconditionally and would fault on a stage of width 794 with a looping layer. No such
 * stage ships, so the guard here is not a behaviour change -- it is the port declining to
 * reproduce a latent crash. */
static int32_t layer_offset(int32_t span, int32_t stage_width, int32_t camera, int32_t w)
{
    if (stage_width <= w) return 0;
    /* LF2_BG_SKEW=<n> shifts every parallax offset by n. It exists so the byte-identity
     * check in tools/background_test.sh has a NEGATIVE case: a frame dump that is identical
     * whatever this function returns would be measuring nothing. Never set in normal use. */
    const char *skew = getenv("LF2_BG_SKEW");
    return -(((span - w) * camera) / (stage_width - w)) + (skew ? atoi(skew) : 0);
}

void fn_0041a250(void)
{
    const uint32_t self = R(ECX);                    /* __thiscall */
    const uint32_t arg0 = LD32(R(ESP) + 4);
    const uint32_t bg = LD32(BG_INDEX);

    /* LF2_BG_ORIG=1 hands every frame to the recompiled body instead. It is the A/B this
     * file is verified by, not a fallback anyone should need: tools/background_test.sh runs
     * the same route both ways and asserts the dumped frames are BYTE-IDENTICAL. A
     * reimplementation that cannot be diffed against what it replaces is a rewrite.
     *
     * The comparison is only worth anything because it can fail: the test also runs an arm
     * with the parallax deliberately off by one, and asserts THAT differs. Two runs that
     * agree prove nothing if the dump would agree no matter what was drawn. */
    if (getenv("LF2_BG_ORIG")) { fn_0041a250__orig(); return; }

    /* Index 99 is a different pass entirely (fn_0041a050) and nothing here applies to it.
     * The original body is kept callable for exactly this, and for the escape below. */
    if (bg == BG_ALT_PASS) { fn_0041a250__orig(); return; }

    const uint32_t registry = LD32(self + 2004);
    const uint32_t base = registry + bg * BG_STRIDE_DW * 4u;
    const int32_t count = (int32_t)LD32(base + BG_LAYER_COUNT);
    if (count <= 0) { R(ESP) += 8; return; }         /* RET 4: return address and one arg */

    /* A record with more layers than the table can hold would mean the field constants are
     * wrong, not that the stage is unusual. Say so once and hand the frame back to the
     * original body rather than drawing a stage from addresses that are not the stage's. */
    if (count > BG_MAX_LAYERS) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "background: layer count %d for background %u exceeds the %d the "
                            "table holds -- the layer field constants do not describe this "
                            "record, so the game's own body is drawing this stage\n",
                    count, bg, BG_MAX_LAYERS);
        }
        fn_0041a250__orig();
        return;
    }

    const int32_t stage_width = (int32_t)LD32(base + BG_STAGE_WIDTH);
    const int32_t camera = (int32_t)LD32(BG_CAMERA_X);

    for (int i = 0; i < (int)count; i++) {
        const uint32_t tint = lf(registry, bg, BG_LAYER_TINT, i);
        if (tint) { fill_layer(registry, bg, i, tint); continue; }

        const int32_t span = (int32_t)lf(registry, bg, BG_LAYER_SPAN, i);
        const int32_t off = layer_offset(span, stage_width, camera, BG_SCREEN_W);

        /* The frame counter is stepped whether or not this frame draws -- it is what makes
         * the animation run -- so it must happen before the range test, not inside it. */
        const int32_t cc = (int32_t)lf(registry, bg, BG_LAYER_CC, i);
        if (cc > 0) {
            const int32_t next = ((int32_t)lf(registry, bg, BG_LAYER_ANIM, i) + 1) % cc;
            lf_store(registry, bg, BG_LAYER_ANIM, i, (uint32_t)next);
            if (next < (int32_t)lf(registry, bg, BG_LAYER_C1, i)) continue;
            if (next > (int32_t)lf(registry, bg, BG_LAYER_C2, i)) continue;
        }

        const uint32_t obj = lf(registry, bg, BG_LAYER_OBJ, i);
        const uint32_t pic = lf(registry, bg, BG_LAYER_PIC, i);
        const int32_t y = (int32_t)lf(registry, bg, BG_LAYER_Y, i);
        const int32_t lx = (int32_t)lf(registry, bg, BG_LAYER_X, i);
        const int32_t loop = (int32_t)lf(registry, bg, BG_LAYER_LOOP, i);

        if (loop > 0) {
            for (int32_t x = lx; x < span; x += loop)
                draw_layer(obj, off + x, y, pic, arg0);
        } else {
            /* loop < 0 would spin for ever; the game would too, but it is a data error and
             * drawing the layer once is the closest thing to what was meant. */
            draw_layer(obj, off + lx, y, pic, arg0);
        }
    }

    R(ESP) += 8;                                     /* RET 4: return address and one arg */
}
