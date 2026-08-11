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
#include "geom.h"

#include "../com.h"
#include "../guest_ops.h"
#include "../hostwin.h"

#include <stdio.h>
#include <stdlib.h>

void fn_0041a250__orig(void);
void fn_0041a5a0__orig(void);

enum { BG_ALT_PASS = 99 };              /* the index fn_0041a250 hands to fn_0041a050 */
/* The camera's shadow copy and the two words that gate it, straight out of fn_0041b5d0:
 * when both gates are set the camera is READ from the mirror at 0x0041bc2f and written back
 * to it at 0x0041bc6e. What they select is not identified and does not need to be -- the
 * clamp only has to leave the mirror agreeing with the camera it clamped. */
enum { CAM_MIRROR    = 0x00450b7c,
       CAM_MIRROR_ON_A = 0x00450b74,
       CAM_MIRROR_ON_B = 0x00450b84 };
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
    /* THIS FILL IS A WORLD BAND, AND THIS IS THE ONLY PLACE THAT KNOWS IT (issue #42).
     *
     * fn_00415160 is the game's ONE colour-fill helper -- the stage's tinted layers and the
     * front end's screen backdrop both go through it -- so by the time a fill reaches Blt
     * nothing in it says which of the two it was. runtime/ddraw.c used to guess from the
     * rectangle: "0 to 794 is the whole native width, so it is a full-width band, stretch it
     * across the viewport". That test matches the FRONT END exactly, because the front end's
     * backdrop is a fill of the whole 794-wide screen -- so a wide window painted the menu's
     * blue across the entire composition and then the centring shift moved it right, leaving
     * black down the left. It also could not have been right in general: a tinted layer is
     * filled at its OWN authored span (BG_LAYER_SPAN just above), which on the shipped stages
     * is 794 only when the stage happens to be exactly one screen wide.
     *
     * So the answer comes from the call structure instead of from a rectangle. The background
     * pass is an override, so it can simply say. Same shape as shadow_hint_set. */
    world_band_hint_set(1);
    guest_call(DRAW_FILL, args, 5);
    world_band_hint_set(0);
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
    /* The formula, and the pin for a layer with less picture than the view is wide, are
     * geom_layer_offset -- checked by runtime/test_geom.c without booting the game. The
     * pin is issue #23: every stage's sky is non-looping and only just wider than 794, so
     * beyond that width the band beside it is black, and filling it would mean inventing
     * layout the stage does not have. */
    if (stage_width <= w || span <= w) return 0;      /* pinned: the skew must not move it */
    const int32_t off = geom_layer_offset(span, stage_width, camera, w);
    /* LF2_BG_SKEW=<n> shifts every parallax offset by n. It exists so the byte-identity
     * check in tools/background_test.sh has a NEGATIVE case: a frame dump that is identical
     * whatever this function returns would be measuring nothing. Never set in normal use.
     * Read once -- this runs for every layer of every frame. */
    static int skew = -1;
    if (skew < 0) { const char *s = getenv("LF2_BG_SKEW"); skew = s ? atoi(s) : 0; }
    return off + skew;
}

/* ---- WHERE THE WIDE VIEW IS CENTRED (issue #39) ----
 *
 * The game puts the players' centroid in the middle of a 794-WIDE window, and it says so in
 * one instruction -- fn_0041b5d0 at 0x0041bb7d, `SUB ESI,0x18d`, where 0x18d is 794/2 and ESI
 * is the mean of the live players' x. Widen the view without touching that and the centroid
 * keeps sitting 397 px from the LEFT edge, so every extra pixel of picture appears on the
 * RIGHT and the game looks like the 4:3 view with a strip bolted onto one side. It did.
 *
 * So the world is DRAWN from a camera shifted left by half the extra width. That puts the
 * game's own 794 view in the middle of the wider one, which is what centring means here.
 *
 * WHY THIS IS A DRAW-TIME VALUE AND NOT A WRITE TO THE CAMERA, which is the trap: fn_0041b5d0
 * EASES the camera toward its target by a seventh (the IDIV at 0x0041bbc6) and reads back
 * whatever is in BG_CAMERA_X to do it. Subtracting the offset there each frame -- the way the
 * clamp above safely does, because a clamp is idempotent and a shift is not -- has fixed point
 * c* = target - 7*K. The view ends up SEVEN TIMES further off than asked for, and it drifts
 * there gradually, so it reads as a wandering camera rather than as a wrong constant.
 *
 * Instead the game's camera is left exactly as the game computed it, and the shifted value is
 * used only while the world is being drawn: here for the parallax, and inside the fn_0041a5a0
 * wrapper below for the objects. Nothing outside those two ever sees it.
 *
 * Clamped at zero because there is no world left of the stage's start: at the left edge the
 * view degrades to what it did before -- the extra width on the right -- rather than opening a
 * band of nothing. At the game's own 794 the offset is exactly zero, which is why
 * tools/background_test.sh's byte-identity arm still holds. */
static long cam_frames, cam_shifted, cam_locked, cam_lock_bound;
static int32_t cam_game_max, cam_draw_max, cam_k, cam_lock_max;

int bg_draw_camera(void)
{
    const int32_t cam = (int32_t)LD32(BG_CAMERA_X);
    const int32_t view = (int32_t)bg_view_width();
    /* The shift is geom_draw_camera; what stays here is reading the guest's camera and the
     * counters LF2_CAMERA reports. `k` is recomputed only to report it. */
    const int32_t c = (int32_t)geom_draw_camera(cam, view);
    const int32_t k = (view - BG_SCREEN_W) / 2;
    cam_k = k > 0 ? k : 0;
    cam_frames++;
    if (c != cam) cam_shifted++;
    if (cam > cam_game_max) cam_game_max = cam;
    if (c   > cam_draw_max) cam_draw_max = c;
    return (int)c;
}

/* LF2_CAMERA=1: whether the wide view was actually re-centred, and if not, WHY not.
 *
 * The offset is only applied where there is world to move into -- it is clamped at the
 * stage's left edge -- so a run can be perfectly correct and shift NOTHING. Brokeback Clif is
 * 1500 wide, so at a 1920 view the whole stage already fits, the camera never leaves 0 and
 * there is nothing to centre. Reporting "0 of 900 frames shifted" without that reason would
 * read as a broken feature, which is exactly the kind of silence this port does not accept. */
void bg_camera_report(void)
{
    if (!getenv("LF2_CAMERA")) return;
    const int view = bg_view_width();
    const int32_t stage = (int32_t)bg_stage_field(BG_STAGE_WIDTH);
    fprintf(stderr, "camera: view %d, centring offset %d; the game's camera reached %d and the "
                    "drawing camera %d over %ld frame(s)\n",
            view, cam_k, cam_game_max, cam_draw_max, cam_frames);
    /* The section lock is what stage mode uses to hold the camera until a section is cleared,
     * and it is ZERO in VS mode. Reporting it is how a route can show it reached stage mode at
     * all -- and it is the only evidence issue #36's clamp has ever run. */
    if (cam_locked)
        fprintf(stderr, "camera: the stage-mode section lock was set on %ld frame(s), reaching "
                        "%d, and BOUND the camera on %ld of them -- so this run entered stage "
                        "mode%s\n", cam_locked, cam_lock_max, cam_lock_bound,
                cam_lock_bound ? " and the lock's view substitution did work (issue #36)"
                               : ", but the stage's own bound was always tighter, so the "
                                 "lock's view substitution was NOT exercised");
    else
        fprintf(stderr, "camera: the stage-mode section lock was NEVER set, so this run did not "
                        "enter stage mode and says nothing about issue #36\n");
    if (cam_shifted)
        fprintf(stderr, "camera: %ld of %ld frames were re-centred, so the wide view is "
                        "centred on what the 4:3 view showed rather than extended right\n",
                cam_shifted, cam_frames);
    else if (cam_k <= 0)
        fprintf(stderr, "camera: NOTHING was re-centred, and correctly so -- the view is the "
                        "game's own %d, where the offset is zero by definition\n", BG_SCREEN_W);
    else if (stage <= view)
        fprintf(stderr, "camera: NOTHING was re-centred -- the stage is %d wide and the view "
                        "is %d, so the whole stage already fits and the camera never left 0. "
                        "There is no world to centre into\n", stage, view);
    else
        fprintf(stderr, "camera: NOTHING was re-centred although the offset is %d and the "
                        "stage (%d) is wider than the view (%d) -- the camera never got past "
                        "the offset, so this run does NOT exercise the centring\n",
                cam_k, stage, view);
}

/* fn_0041a5a0 -- the stage's object pass: it collects every live object, depth-sorts them and
 * draws them. It reads the camera nine times and every one is a `SUB reg, camera` turning a
 * world x into a screen x; it writes the camera never, and writes no world state through it.
 * That is what makes this wrapper safe -- it changes where things are drawn and nothing else.
 *
 * The shift is applied and removed inside one call, so BG_CAMERA_X is never observed shifted
 * by anything else, and there is no way to leak a shifted camera into the next frame's ease. */
void fn_0041a5a0(void)
{
    const uint32_t saved = LD32(BG_CAMERA_X);
    ST32(BG_CAMERA_X, (uint32_t)bg_draw_camera());
    fn_0041a5a0__orig();
    ST32(BG_CAMERA_X, saved);
}

/* The width the layers are drawn into: the game's 794, or the widescreen composition when
 * one is up. This ONE substitution is the whole of the widescreen change in this file --
 * the game's own formula, with its screen width made the real one. */
int bg_view_width(void)
{
    const int w = lf2_wide_width();
    return w > BG_SCREEN_W ? w : BG_SCREEN_W;
}

/* Issue #28: the camera is still clamped to the 4:3 limit, so a wider view scrolls past the
 * wall a character can walk to.
 *
 * The game's own clamp is at 0x0041bc47..0x0041bc60, inside fn_0041b5d0:
 *
 *     if (camera < 0)                     camera = 0;
 *     if (camera > stage_width - 794)     camera = stage_width - 794;
 *
 * with 794 a literal, exactly as in the parallax. Re-applied here with the real view width.
 *
 * WHY IT IS APPLIED HERE rather than in fn_0041b5d0, and why that is not a workaround. The
 * clamp has to land between the camera update and anything that draws from it, and
 * fn_0041b5d0 does BOTH -- it updates the camera and then calls this function as its last
 * act (0x0041bc7b). So the top of the layer draw IS that boundary, from the game's own
 * structure rather than by luck: fn_0041a250 has exactly two call sites, and the other
 * (0x0041d748) is immediately before the object draw fn_0041a5a0, so the clamp lands ahead
 * of the sprites too. Nothing reads the camera in between at either site. Overriding
 * fn_0041b5d0 (1722 bytes) to move it a few instructions earlier would change no pixel.
 *
 * A view wider than the whole stage cannot be helped by any camera value -- there is less
 * world than view -- so the maximum floors at 0 and the remainder is black. That is the
 * stage being narrower than the window, not a scrolling fault. */
static void camera_clamp_to_view(int32_t stage_width, int32_t view)
{
    int32_t camera = (int32_t)LD32(BG_CAMERA_X);

    /* THE STAGE-MODE SECTION LOCK gets the same substitution, and for the same reason
     * (issue #36). fn_0041b5d0 bounds the camera a second time by [0x00450bb0] when that is
     * non-zero, which is what holds the camera partway along a stage until the section is
     * cleared. Both bounds are the game saying "the RIGHT EDGE OF THE SCREEN goes here", and
     * both say it in terms of a 794-wide screen -- so on a wider view the camera stopped at
     * the same world position and the player could see well past where they were allowed to
     * walk.
     *
     * `lock + 794 - view` puts the right edge exactly where the 4:3 game puts it, which is
     * the whole of the request: the camera stops the same distance from the walk boundary
     * whatever the window is. At view == 794 it is `lock` unchanged, so this cannot alter
     * the game's own picture -- which is what tools/background_test.sh's byte-identity arm
     * checks.
     *
     * NOT VERIFIED IN STAGE MODE ITSELF: every scripted route this port has reaches VS mode,
     * where the lock reads 0 and this branch never runs. The substitution is the same one the
     * bound above it already gets and it is a no-op at the game's own width, but nobody has
     * watched it hold a camera in a stage. */
    const int32_t lock = (int32_t)LD32(BG_CAMERA_LOCK);
    /* Both bounds and the floor at zero are geom_camera_max, which runtime/test_geom.c checks
     * at the game's own width and wider. What is here is the guest read and the counters. */
    const int32_t max = (int32_t)geom_camera_max(stage_width, view, lock);
    if (lock) {
        cam_locked++;
        if (lock > cam_lock_max) cam_lock_max = lock;
        /* Counted separately from "the lock was set": the substitution only DOES anything
         * when the lock is the binding constraint, and a run where the stage bound was
         * always tighter would exercise none of it while still reporting a lock. */
        if (lock + BG_SCREEN_W - view < stage_width - view) cam_lock_bound++;
    }
    if (camera > max) camera = max;
    if (camera < 0) camera = 0;
    ST32(BG_CAMERA_X, (uint32_t)camera);

    /* The game mirrors the camera into CAM_MIRROR under the same two conditions it reads it
     * back from there (0x0041bc2f / 0x0041bc6e). Keeping the mirror in step matters because
     * that read happens BEFORE the clamp on the next frame -- leaving it stale would let the
     * unclamped value come back round. */
    if (LD32(CAM_MIRROR_ON_A) && LD32(CAM_MIRROR_ON_B)) ST32(CAM_MIRROR, (uint32_t)camera);
}

void fn_0041a250(void)
{
    const uint32_t self = R(ECX);                    /* __thiscall */
    const uint32_t arg0 = LD32(R(ESP) + 4);
    const uint32_t bg = LD32(BG_INDEX);

    /* LF2_BG_ORIG=1 hands every frame to the recompiled body instead. It is the A/B this
     * file is verified by, not a fallback anyone should need: tools/background_test.sh runs
     * the same route five ways and asserts that at 794x550 the dumped frames are
     * BYTE-IDENTICAL to this body's, and that at 1600x550 they are not. A reimplementation
     * that cannot be diffed against what it replaces is a rewrite.
     *
     * The identity is only worth anything because it can fail, which is what LF2_BG_SKEW is
     * for. Two runs agreeing proves nothing if the dump would agree whatever was drawn. */
    static int use_orig = -1;
    if (use_orig < 0) use_orig = getenv("LF2_BG_ORIG") != NULL;
    if (use_orig) { fn_0041a250__orig(); return; }

    /* Index 99 is a different pass entirely (fn_0041a050) and nothing here applies to it.
     * The original body is kept callable for exactly this, and for the escape below. */
    if (bg == BG_ALT_PASS) {
        /* Background 99 is a different pass entirely (fn_0041a050) and reads the camera
         * itself, so it gets the same treatment as the object pass. */
        const uint32_t saved = LD32(BG_CAMERA_X);
        ST32(BG_CAMERA_X, (uint32_t)bg_draw_camera());
        fn_0041a250__orig();
        ST32(BG_CAMERA_X, saved);
        return;
    }

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
    const int32_t view = bg_view_width();
    camera_clamp_to_view(stage_width, view);
    /* The layers are part of the world, so they are drawn from the same shifted camera the
     * objects are -- their parallax included, because a re-centring IS a camera pan and a
     * nearer layer must move further than a distant one. */
    const int32_t camera = (int32_t)bg_draw_camera();

    for (int i = 0; i < (int)count; i++) {
        const uint32_t tint = lf(registry, bg, BG_LAYER_TINT, i);
        if (tint) { fill_layer(registry, bg, i, tint); continue; }

        const int32_t span = (int32_t)lf(registry, bg, BG_LAYER_SPAN, i);
        const int32_t off = layer_offset(span, stage_width, camera, view);

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
            /* The game stops at the layer's span, which is exactly enough to fill 794. A
             * wider view needs more copies, and a LOOPING layer is the one kind that can
             * honestly provide them: the repeat step is the layer's own declared `loop:`,
             * so carrying it on is the stage's layout continued rather than invented. At
             * the game's own 794 this adds nothing -- a layer's span always reaches the
             * right edge there, which is why the native-width arm of the test still comes
             * out byte-identical. */
            for (int32_t x = lx; x < span || off + x < view; x += loop)
                draw_layer(obj, off + x, y, pic, arg0);
        } else {
            /* loop < 0 would spin for ever; the game would too, but it is a data error and
             * drawing the layer once is the closest thing to what was meant. */
            draw_layer(obj, off + lx, y, pic, arg0);
        }
    }

    R(ESP) += 8;                                     /* RET 4: return address and one arg */
}
