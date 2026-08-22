/* fn_0041a250 -- the stage's background layer draw.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 *
 * WHY THIS IS AN OVERRIDE AND NOT A BLIT-PATH HEURISTIC. Widescreen has to decide, for every
 * background layer, whether there is more picture to show beside the game's 794 and at what
 * step to repeat it. The blit stream cannot answer that: a layer that repeats and a layer
 * that does not look identical by the time they reach Blt, and runtime/video/ddraw.c's contiguity
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
 * WHAT FOLLOWS FROM IT for widescreen (issues #23/#87 and claim C017): a non-looping layer
 * has no additional picture to uncover. Looping layers continue at their declared step.
 * A wider view never changes a bitmap's dimensions or the layers' shared world origin.
 * backdrop_layout.h declares the one opaque far plane whose edge may continue behind them.
 *
 * Verified against the body it replaces by drawing the same frames both ways: see
 * tools/routes/background_test.sh.
 */

#include "overrides.h"
#include "world.h"
#include "geom.h"

#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "stagegeom.h"
#include "render.h"
#include "backdrop.h"
#include "backdrop_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fn_0041a250__orig(void);
void fn_0041a5a0__orig(void);

/* The camera's shadow copy and the two words that gate it, straight out of fn_0041b5d0. */
enum { CAM_MIRROR    = 0x00450b7c,
       CAM_MIRROR_ON_A = 0x00450b74,
       CAM_MIRROR_ON_B = 0x00450b84 };

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

/* One picture, at one place. The clip index is -1 -- the whole picture, not a glyph.
 *
 * `transparent` is bg.dat's `transparency:` straight out of the record, and it is the fourth
 * argument because that is what fn_0043f010 does with it: `-(arg != 0) & 0x8000` ORed into the
 * blit flags, i.e. the colour key. This used to be called `pic` on the strength of a field name
 * that was itself a guess; two readings agree it is the transparency flag -- fn_0040c160 scans
 * `transparency:` into that field, and fn_0043f010 uses it as a key enable. */
static void draw_layer(uint32_t obj, int32_t x, int32_t y, uint32_t transparent, uint32_t arg0)
{
    const uint32_t args[6] = { (uint32_t)x, (uint32_t)y, 0xffffffffu, transparent, 0u, arg0 };
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
     * nothing in it says which of the two it was. runtime/video/ddraw.c used to guess from the
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
     * geom_layer_offset -- checked by tests/test_geom.c without booting the game. The pin is
     * issue #23: every stage's sky is non-looping and only just wider than 794. Any declared
     * native-size continuation is placed later; parallax itself remains the game's formula. */
    if (stage_width <= w || span <= w) return 0;      /* pinned: the skew must not move it */
    const int32_t off = geom_layer_offset(span, stage_width, camera, w);
    /* LF2_BG_SKEW=<n> shifts every parallax offset by n. It exists so the byte-identity
     * check in tools/routes/background_test.sh has a NEGATIVE case: a frame dump that is identical
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
 * tools/routes/background_test.sh's byte-identity arm still holds. */
static long bg_alt_frames;
static long cam_frames, cam_shifted, cam_locked, cam_lock_bound, cam_walk_widened;
static int32_t cam_walk_max;
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
    /* Issue #58's open question, answered by every run that prints this. */
    if (bg_alt_frames)
        fprintf(stderr, "camera: the game's BUILT-IN background (index %d, fn_0041a050) was "
                        "drawn on %ld of those frames -- its bands are 794 wide as literals, so "
                        "issue #58 is REACHABLE and visible in a wide view\n",
                BG_ALT_PASS, bg_alt_frames);
    else
        fprintf(stderr, "camera: the built-in background (index %d, fn_0041a050) was never "
                        "selected in %ld frame(s), so this run says nothing about issue #58 -- "
                        "it does NOT show the backdrop is unreachable, only that this route did "
                        "not reach it\n", BG_ALT_PASS, cam_frames);
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
    /* The walk bound, and the negative carries its own reason. Three different runs report
     * zero here and they mean three different things -- VS mode has no section at all, a 794
     * view is the game's own answer by construction, and a wide view whose camera always
     * reached its bound had nothing to widen. A bare "0" would read as the fix not working. */
    if (!cam_locked)
        fprintf(stderr, "camera: no walk bound was widened because this run set no section "
                        "lock at all, which is VS mode -- it says nothing about issue #43\n");
    else if (view <= BG_SCREEN_W)
        fprintf(stderr, "camera: no walk bound was widened, and correctly so -- the view is "
                        "the game's own %d, where geom_walk_max returns the game's B by "
                        "construction\n", BG_SCREEN_W);
    else if (cam_walk_widened)
        fprintf(stderr, "camera: the walk bound was widened to the screen's right edge on %ld "
                        "frame(s), reaching %d -- so a fighter can reach every part of the "
                        "stage this %d-wide view shows (issue #43)\n",
                cam_walk_widened, cam_walk_max, view);
    else
        fprintf(stderr, "camera: the view is %d and a section lock was set, but NO walk bound "
                        "needed widening -- the camera reached its bound on every frame, so "
                        "the screen's right edge already sat on the game's own B\n", view);
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

/* fn_00415160 as fn_0041a050 calls it: cdecl, five args, the caller pops. Marked as a world
 * band for the same reason fill_layer is -- the fill helper is shared with the front end and
 * the blit path cannot tell the two apart (issue #42). */
static void alt_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t colour)
{
    const uint32_t args[5] = { (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, colour };
    world_band_hint_set(1);
    guest_call(DRAW_FILL, args, 5);
    world_band_hint_set(0);
    R(ESP) += 5 * 4;
}

/* fn_0041a050 -- the game's BUILT-IN background, hand-ported (issue #58).
 *
 * WHY. It draws a fixed backdrop rather than a stage's layers, and every one of its solid bands
 * is 794 wide as a literal -- the game saying "the whole width of the screen" in the only units
 * it had. On a 978-wide composition they cover 81% of it and the rest goes unpainted. Same
 * substitution as the layer pass and the object pass: 794 is the VIEW, not a constant. It has to
 * be an override for the same reason fn_0041a5a0 did -- the widths are immediates in recompiled
 * code, so there is no address to write.
 *
 * IT IS A BACKGROUND A PLAYER CHOOSES, which is what made it worth doing: index 99 sits inside
 * the range the stage chooser cycles (0042d7f6 maps the pick `count - 3` onto it; 004339a4 and
 * 00435b48 wrap 100 back to it). No scripted route has ever drawn it only because every route
 * takes the default background.
 *
 * __thiscall, one stack arg, RET 4. The three clip draws take their receiver from the background
 * record -- Ghidra elides it at every call site, as it does throughout this binary.
 *
 * LF2_ALTBG_ORIG=1 runs the recompiled body instead, for the byte-identity A/B.
 */
enum { ALTBG_SKY   = 0x4d81978,      /* [[this+0x7d4]+off] -- the receiver of each clip draw */
       ALTBG_CLOUD = 0x4d8197c,
       ALTBG_POST  = 0x4d81974 };

void fn_0041a050__orig(void);

void fn_0041a050(void)
{
    if (getenv("LF2_ALTBG_ORIG")) { fn_0041a050__orig(); return; }

    const uint32_t self = R(ECX);
    const uint32_t arg0 = LD32(R(ESP) + 4);
    const uint32_t rec  = LD32(self + 0x7d4);
    const int32_t  cam  = (int32_t)LD32(BG_CAMERA_X);
    /* THE ONE SUBSTITUTION. Everything below that was 0x31a is this, and the fence's bound
     * 0x319 is one less than it, exactly as the game wrote them. */
    const int32_t  w    = bg_view_width();

    draw_layer(LD32(rec + ALTBG_SKY), 0xfa - cam / 100, 0x78, 0, arg0);

    for (int32_t i = 0; i < 4000; i += 500)
        draw_layer(LD32(rec + ALTBG_CLOUD), (i - (cam * 7) / 10) + 0x1e, 0xaf, 1, arg0);

    alt_fill(0, 0x146, w, 0x14, 0x3f3f3f);
    alt_fill(0, 0x159, w, 0x9c, 0x575757);
    alt_fill(0, 0x1d7, w, 0x1e, 0x3f3f3f);
    alt_fill(0, 0x148, w, 2,   0x373737);

    for (int32_t x = (900 - cam) % 0x46; x < w - 1; x += 0x46) {
        alt_fill(x,     0x124, 1, 0x25, 0x577fa7);
        alt_fill(x + 1, 0x124, 1, 0x25, 0x2f4357);
    }

    alt_fill(0, 0x136, w, 1, 0x577fa7);
    alt_fill(0, 0x137, w, 1, 0x2f4357);
    alt_fill(0, 0x122, w, 1, 0x577fa7);
    alt_fill(0, 0x123, w, 1, 0x2f4357);

    for (int32_t i = 0; i < 0xc80; i += 0x140)
        draw_layer(LD32(LD32(self + 0x7d4) + ALTBG_POST), (i - cam) + 10, 0x186, 0, arg0);

    R(ESP) += 4 + 4;                  /* RET 4: the return address and one stack arg */
}

/* fn_0041a5a0 -- the stage's object pass -- is now HAND-PORTED, in
 * runtime/overrides/objects.c. The camera wrapper that used to sit here existed only because
 * the body was recompiled: it wrote the shifted camera into the guest's word around the call so
 * the lifted `SUB reg, camera` sites would draw shifted. The port reads the drawing camera
 * directly at each of those sites, so there is nothing left to wrap (issue #55).
 */

/* The width the layers are drawn into: the game's 794, or the widescreen composition when
 * one is up. This ONE substitution is the whole of the widescreen change in this file --
 * the game's own formula, with its screen width made the real one. */
int bg_view_width(void)
{
    const int w = lf2_wide_width();
    return w > BG_SCREEN_W ? w : BG_SCREEN_W;
}

/* ---- the stage's hand-woven geometry (issue #62) ----
 *
 * This file is where it is loaded because this file already holds everything it needs: the
 * stage record, the layer spans, the view width and the draw-time camera. docs/stage-geometry.md
 * is the format; runtime/video/stagegeom.c is the loader, which knows nothing about the guest
 * on purpose so that ctest can walk it offline.
 *
 * THE LOOKUP is what connects the two. An author writes `depth: layer hill1.bmp` and this
 * resolves that name against the loaded stage's own layers -- the record carries each layer's
 * bitmap path (claim C033) -- so the solid takes the depth the DATA gives that layer (C031)
 * rather than a constant that goes stale. A name the stage does not have is refused by the
 * loader, not defaulted.
 */
static int stage_layer_lookup(void *ctx, const char *layer, int *span, int *stage_width)
{
    (void)ctx;
    const int n = bg_layer_count();
    for (int i = 0; i < n; i++) {
        const char *name = bg_layer_name(i);
        if (!name || strcasecmp(name, layer) != 0) continue;
        *span = (int)(int32_t)bg_layer_field(BG_LAYER_SPAN, i);
        *stage_width = (int)(int32_t)bg_stage_field(BG_STAGE_WIDTH);
        return 1;
    }
    return 0;
}

/* The loaded stage's geometry, reloaded when the stage changes and NOT once per frame. A
 * stage is identified by its background index, which is the same word fn_0041a250 draws from. */
static StageGeom stage_geom;
static int       stage_geom_bg = -1;    /* the index stage_geom was loaded for */
static int       stage_geom_tried;      /* a load was attempted, so 0 vertices means 0 */
static int       geom_planned_bg = -1;  /* the stage the gap plan below was built for */

static void stage_geom_sync(void)
{
    const int bg = (int)LD32(BG_INDEX);
    if (stage_geom_tried && bg == stage_geom_bg) return;
    stagegeom_free(&stage_geom);
    geom_planned_bg = -1;
    stage_geom_bg = bg;
    stage_geom_tried = 1;

    const char *name = bg_stage_name();
    if (!name || !*name) return;         /* no stage loaded yet: try again next frame */

    /* WHERE `stages/` IS, and why it is not simply the working directory. The process runs
     * with its cwd in the GAME TREE, because the game opens all of its own data by relative
     * path -- and the game tree is neither in this repo nor shipped by it. Authored geometry
     * is the PORT's content: it is committed here, and it has to reach the running program
     * without a path baked in that only works on the machine that built it.
     *
     * So it is looked for BESIDE THE EXECUTABLE first (CMake copies the repo's `stages/` next
     * to the binary, and an installed port puts it there too), and in the working directory
     * second, which is what makes a drop-in into an existing game tree work. Both are tried
     * and the one that has the file wins; `stagegeom_load` treats a missing file as success
     * with nothing in it, so a directory that is not there is not an error. */
    char beside[512];
    const char *dirs[2];
    int nd = 0;
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(beside, sizeof beside, "%.480sstages", base);   /* base ends in a separator */
        dirs[nd++] = beside;
    }
    dirs[nd++] = "stages";

    for (int i = 0; i < nd; i++) {
        if (!stagegeom_load(dirs[i], name, stage_layer_lookup, NULL, &stage_geom)) {
            /* A file that exists and is wrong is reported EVERY time the stage is entered,
             * not once: an author fixing a .stage file re-enters the stage to see whether it
             * worked, and a once-only message would go quiet exactly then. */
            fprintf(stderr, "stage geometry: %s/%s.stage was REFUSED -- %s\n",
                    dirs[i], name, stage_geom.error);
            return;
        }
        if (stage_geom.n) break;
    }

    if (stage_geom.n == 0) {
        /* Not an error -- it is the state of every stage until one is authored -- and said
         * out loud anyway, because "this stage has no authored geometry" and "the loader
         * never ran" are the two things this whole subsystem can fail to distinguish. It
         * names every directory it looked in, so a file in the wrong place reads as a file in
         * the wrong place rather than as a stage nobody has woven yet. */
        if (getenv("LF2_STAGE_GEOM")) {
            fprintf(stderr, "stage geometry: %s has no <dir>/%s.stage, so nothing is woven "
                            "into it -- this stage draws exactly as it always has. Looked "
                            "in:\n", name, name);
            for (int i = 0; i < nd; i++)
                fprintf(stderr, "stage geometry:   %s\n", dirs[i]);
        }
        return;
    }
    fprintf(stderr, "stage geometry: %s -- %d solid(s), %d vertices, %d OBJ line(s) this "
                    "loader does not read\n",
            name, stage_geom.solids, stage_geom.n, stage_geom.skipped_lines);
    /* THE DEPTHS, and they are the half of this worth printing. A count of vertices says the
     * file parsed; a depth says the solid landed in the plane it was authored for, which is
     * the thing `depth: layer <file>` exists to get right and the thing that goes silently
     * wrong -- a solid at the fighters' plane instead of the far hill looks like geometry, not
     * like a bug. Printed per solid, by watching the depth change down the vertex list. */
    for (int i = 0; i < stage_geom.n; i++) {
        if (i && stage_geom.v[i].depth == stage_geom.v[i - 1].depth) continue;
        fprintf(stderr, "stage geometry:   solid at depth %.4f (%s)\n",
                (double)stage_geom.v[i].depth,
                stage_geom.v[i].depth > 1.0f ? "further than the fighters" :
                stage_geom.v[i].depth < 1.0f ? "nearer than the fighters"  :
                                               "the fighters' own plane");
    }
}

/* ---- WHERE the geometry goes in the painter order, which is the whole of the submission ----
 *
 * A finished geometry pass composites as ONE full-screen quad, and one quad enters the game's
 * painter order at ONE point. That is not enough, because a hand-woven set spans parallax
 * depths and the game paints its OWN layers between them: The Great Wall's `road3` is in front
 * of the fighters while its `sky` is 267 deep, so a set with a far pillar and a near railing
 * has layers that belong BETWEEN its two solids. "Behind every layer" and "in front of every
 * layer" are both wrong for it -- and each would look perfectly right on whichever stage
 * happened to have all its solids on one side, which is exactly the kind of wrong that ships.
 *
 * So the pass runs once per OCCUPIED GAP. The rule is the game's own order extended to
 * authored geometry: a solid at parallax depth d is drawn immediately before the first layer
 * whose derived depth is <= d -- i.e. after everything it is in front of nothing of, and
 * before the first thing it is in front of. A solid nearer than every layer goes after all of
 * them, still behind the sprites.
 *
 * A layer's depth is derived, not authored (claim C031): `(stage_width - 794)/(span - 794)`.
 * geom_layer_depth returns 0 where that is not derivable -- a stage that never pans, or a
 * layer that never moves -- and 0 means INFINITELY FAR here, so it sorts behind everything.
 * Comparing 0 as a small number would put the sky in front of the fighters.
 */
enum { GEOM_GAPS_MAX = 8 };     /* mesh.c's MESH_SLOTS; each gap needs its own live target */

typedef struct { int gap, first, count; } GeomRun;   /* one solid: a run of equal depth */
static GeomRun   geom_runs[64];
static int       geom_nruns;
static int       geom_gaps[GEOM_GAPS_MAX];           /* the occupied gaps, ascending */
static int       geom_ngaps;
/* ONE PERSISTENT BUFFER PER GAP, built when the plan is, not per frame. The display list holds
 * a reference to these, so they must outlive the frame -- and a stage's geometry does not change
 * while the stage is loaded, so rebuilding them per frame would be pure copying. */
static MeshVertex *geom_slice[GEOM_GAPS_MAX];
static int         geom_slice_n[GEOM_GAPS_MAX];
static long        geom_frames, geom_submits, geom_no_surface, geom_over_gaps;

/* How deep is layer `i`, with 0 meaning infinitely far. */
static float layer_depth(int i, int32_t stage_width)
{
    const int32_t span = (int32_t)bg_layer_field(BG_LAYER_SPAN, i);
    const float d = geom_layer_depth((int)span, (int)stage_width);
    return d > 0.0f ? d : 1e30f;         /* not derivable == never moves == infinitely far */
}

/* Which gap a solid at depth `d` belongs in: the index of the first layer it is in front of. */
static int gap_for_depth(float d, int count, int32_t stage_width)
{
    if (!(d > 0.0f)) d = 1e30f;          /* the loader refuses this, but the rule is total */
    for (int i = 0; i < count; i++)
        if (layer_depth(i, stage_width) <= d) return i;
    return count;                        /* nearer than every layer */
}

/* Group the loaded vertices into solids and assign each a gap. Recomputed per frame because a
 * layer's depth comes from the record, and a stage the port has not seen yet has none. */
static void geom_plan(int count, int32_t stage_width)
{
    geom_nruns = geom_ngaps = 0;
    for (int i = 0; i < stage_geom.n; ) {
        const float d = stage_geom.v[i].depth;
        int j = i;
        while (j < stage_geom.n && stage_geom.v[j].depth == d) j++;
        if (geom_nruns == (int)(sizeof geom_runs / sizeof geom_runs[0])) {
            /* Said, not silently truncated: a set with more solids than this holds would
             * simply lose its last ones, which looks like art that was never authored. */
            static int said;
            if (!said) {
                said = 1;
                fprintf(stderr, "stage geometry: more than %d solids at distinct depths -- the "
                                "rest are NOT drawn\n",
                        (int)(sizeof geom_runs / sizeof geom_runs[0]));
            }
            break;
        }
        const int gap = gap_for_depth(d, count, stage_width);
        geom_runs[geom_nruns++] = (GeomRun){ gap, i, j - i };
        int seen = 0;
        for (int k = 0; k < geom_ngaps; k++) if (geom_gaps[k] == gap) { seen = 1; break; }
        if (!seen && geom_ngaps < GEOM_GAPS_MAX) geom_gaps[geom_ngaps++] = gap;
        else if (!seen) geom_over_gaps++;
        i = j;
    }
    for (int a = 0; a < geom_ngaps; a++)            /* ascending, so the loop can walk them */
        for (int b = a + 1; b < geom_ngaps; b++)
            if (geom_gaps[b] < geom_gaps[a]) {
                const int t = geom_gaps[a]; geom_gaps[a] = geom_gaps[b]; geom_gaps[b] = t;
            }

    /* Gather each gap's solids into one buffer. Done HERE rather than per frame because the
     * result depends only on the stage's geometry and its layer depths, both of which are fixed
     * while the stage is loaded. */
    for (int k = 0; k < geom_ngaps; k++) {
        int total = 0;
        for (int r = 0; r < geom_nruns; r++)
            if (geom_runs[r].gap == geom_gaps[k]) total += geom_runs[r].count;
        free(geom_slice[k]);
        geom_slice[k] = NULL;
        geom_slice_n[k] = 0;
        if (total <= 0) continue;
        geom_slice[k] = malloc((size_t)total * sizeof *geom_slice[k]);
        if (!geom_slice[k]) continue;
        int at = 0;
        for (int r = 0; r < geom_nruns; r++) {
            if (geom_runs[r].gap != geom_gaps[k]) continue;
            memcpy(geom_slice[k] + at, stage_geom.v + geom_runs[r].first,
                   (size_t)geom_runs[r].count * sizeof *geom_slice[k]);
            at += geom_runs[r].count;
        }
        geom_slice_n[k] = at;
    }
}

/* Record one gap's geometry into the display list at THIS point in the painter order.
 *
 * IT NO LONGER RENDERS ANYTHING HERE, and that is the whole of issue #64 arriving. This used to
 * run a full render pass per gap -- its own colour and depth target, submitted before the list
 * was even drawn -- and hand the finished texture over, because the two renderers could only
 * meet as a texture. The engine takes the vertices and draws them in the SAME pass as the
 * sprites, sharing the one depth buffer. The SDL_Render path still composites per gap, from
 * render.c, because it cannot take geometry at all; both read the same recorded entry.
 *
 * The vertices are a REFERENCE. A stage's geometry is loaded once and submitted every frame, so
 * the slices below live as long as the stage does -- which is exactly the lifetime the display
 * list needs. Copying them per frame would be a memcpy of the whole set sixty times a second
 * for no reason.
 */
static void geom_submit(int gap, int camera, int view_w, int view_h)
{
    int slot = -1;
    for (int k = 0; k < geom_ngaps; k++) if (geom_gaps[k] == gap) { slot = k; break; }
    if (slot < 0 || !geom_slice[slot] || geom_slice_n[slot] <= 0) return;

    const uint32_t dst = frame_source_pixels();
    if (!dst) {
        /* COUNTED. The composition surface is discovered from the game's own copy to the
         * primary, so it is unknown for the first frames of a process -- and "the surface is
         * not known yet" and "there was no geometry" produce the same picture. A count that
         * keeps climbing means it is never discovered, which is a different bug entirely. */
        geom_no_surface++;
        return;
    }
    render_stage_mesh(dst, geom_slice[slot], geom_slice_n[slot], slot,
                      camera, view_w, view_h);
    geom_submits++;
}

void bg_geom_report(void)
{
    if (!getenv("LF2_STAGE_GEOM")) return;
    fprintf(stderr, "stage geometry: %ld frame(s) with geometry, %ld pass(es) placed in the "
                    "display list, %ld dropped for want of a known composition surface, "
                    "%ld solid(s) past the %d-gap limit\n",
            geom_frames, geom_submits, geom_no_surface, geom_over_gaps, GEOM_GAPS_MAX);
    if (geom_frames && !geom_submits)
        fprintf(stderr, "stage geometry: a stage HAS geometry and NOT ONE pass reached the "
                        "frame -- the pass is unavailable (software renderer?), or the "
                        "composition surface was never discovered\n");
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
     * the game's own picture -- which is what tools/routes/background_test.sh's byte-identity arm
     * checks.
     *
     * NOT VERIFIED IN STAGE MODE ITSELF: every scripted route this port has reaches VS mode,
     * where the lock reads 0 and this branch never runs. The substitution is the same one the
     * bound above it already gets and it is a no-op at the game's own width, but nobody has
     * watched it hold a camera in a stage. */
    const int32_t lock = (int32_t)LD32(BG_CAMERA_LOCK);
    /* Both bounds and the floor at zero are geom_camera_max, which tests/test_geom.c checks
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

    /* THE WALK BOUND FOLLOWS THE SAME EDGE (issue #43). The section's two words say one thing
     * between them -- when the camera is at its bound the walkable area ends at the screen's
     * right edge -- and that stops being true as soon as the camera cannot reach its bound,
     * which is what happens near a stage's start once the view is wider than 794. Measured in
     * stage 1-1's first section: lock 106 so B = 900, view 978, camera clamped to 0, and 78
     * world pixels of stage on screen that no fighter could walk to.
     *
     * ONLY WHEN THE GAME HAS SET ONE. The clamp in fn_0041b5d0 is `if (0 < walk)`, so this
     * word is how the game says whether a section boundary exists at all -- it is zero in VS
     * mode. Writing a non-zero value here when the game wrote none would invent a boundary
     * and pen every fighter behind it.
     *
     * SAFE TO WRITE BACK, unlike the camera. The trap issue #39 recorded is that fn_0041b5d0
     * EASES the camera toward its target and reads the word back, so a per-frame adjustment
     * feeds back and settles seven times too far. This word is not eased and is never read to
     * derive itself -- the game writes it from stage data at a section change and only ever
     * compares against it -- so writing the widened bound each frame is idempotent. */
    const int32_t walk = (int32_t)LD32(BG_WALK_LOCK);
    if (walk > 0) {
        const int32_t want = (int32_t)geom_walk_max(walk, max, view);
        if (want != walk) { ST32(BG_WALK_LOCK, (uint32_t)want); cam_walk_widened++; }
        if (want > cam_walk_max) cam_walk_max = want;
    }

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
    uint32_t bg = LD32(BG_INDEX);

    /* LF2_BG_ORIG=1 hands every frame to the recompiled body instead. It is the A/B this
     * file is verified by, not a fallback anyone should need: tools/routes/background_test.sh runs
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
    /* LF2_ALTBG_FORCE=1 draws the BUILT-IN background instead of the loaded stage's layers.
     * It is a diagnostic and it fakes nothing: it takes the branch the game itself takes when
     * the stage chooser lands on index 99, which is the only way a player sees this backdrop
     * (issue #58). Without it the pass is unreachable from any scripted route, and its override
     * could not be verified against the body it replaces. */
    /* It has to write the GUEST WORD, not this local. The alt branch below hands off to
     * fn_0041a250__orig, which reads the index out of 0x0044d024 itself -- so setting only the
     * local left both arms of the A/B drawing the ordinary stage, and they came out identical
     * at 794 AND at 1920. That read as "the port matches the body it replaces" and was really
     * "neither arm ever called the ported function". */
    const int alt_forced = getenv("LF2_ALTBG_FORCE") != NULL;
    const uint32_t bg_saved = LD32(BG_INDEX);
    if (alt_forced) { bg = BG_ALT_PASS; ST32(BG_INDEX, BG_ALT_PASS); }

    if (bg == BG_ALT_PASS) {
        /* COUNTED, because issue #58 turns on whether anyone ever sees this. fn_0041a050 draws
         * a fixed backdrop whose five bands are 794 wide as literals, so it would stop short in
         * a wide view -- but the entry could not say whether any route or any player reaches
         * it, and a fix for a screen nobody sees is worth nothing. bg_camera_report prints this
         * with its denominator so a ZERO is readable as "never selected" rather than as
         * silence. */
        bg_alt_frames++;
        /* Background 99 is a different pass entirely (fn_0041a050) and reads the camera
         * itself, so it gets the same treatment as the object pass. */
        const uint32_t saved = LD32(BG_CAMERA_X);
        ST32(BG_CAMERA_X, (uint32_t)bg_draw_camera());
        fn_0041a250__orig();
        ST32(BG_CAMERA_X, saved);
        if (alt_forced) ST32(BG_INDEX, bg_saved);
        return;
    }

    const uint32_t registry = LD32(self + 2004);
    const uint32_t base = registry + bg * BG_STRIDE_DW * 4u;
    const int32_t count = (int32_t)LD32(base + BG_LAYER_COUNT);
    if (count <= 0) {
        R(ESP) += 8; return;                         /* RET 4: return address and one arg */
    }

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
    stage_geom_sync();       /* reloads only when the stage changes */
    /* The layers are part of the world, so they are drawn from the same shifted camera the
     * objects are -- their parallax included, because a re-centring IS a camera pan and a
     * nearer layer must move further than a distant one. */
    const int32_t camera = (int32_t)bg_draw_camera();
    const long trace_frame = hostwin_frames() + 1;
    const int trace_selected = hostwin_frame_selected(getenv("LF2_BLT_FRAME"), trace_frame);
    if (trace_selected)
        fprintf(stderr,
                "backdrop camera frame %ld guest=%d draw=%d view=%d stage=%d\n",
                trace_frame, (int32_t)LD32(BG_CAMERA_X), camera, view, stage_width);

    /* The hand-woven geometry, planned before the loop and submitted INSIDE it, because where
     * each pass lands in the painter order is the whole point (issue #62). */
    /* Planned when the STAGE changes, not per frame: the gaps depend only on the geometry and
     * the layer depths, and both are fixed while a stage is loaded. Re-planning per frame also
     * meant re-gathering every slice per frame, which the display list's reference to them now
     * makes plainly wrong as well as wasteful. */
    if (stage_geom.n && geom_planned_bg != stage_geom_bg) {
        geom_plan((int)count, stage_width);
        geom_planned_bg = stage_geom_bg;
    } else if (!stage_geom.n) {
        geom_nruns = geom_ngaps = 0;
        geom_planned_bg = -1;
    }
    if (stage_geom.n) geom_frames++;
    int next_gap = 0;

    const char *stage_name = bg_stage_name();

    for (int i = 0; i < (int)count; i++) {
        while (next_gap < geom_ngaps && geom_gaps[next_gap] == i)
            geom_submit(geom_gaps[next_gap++], (int)camera, (int)view, GEOM_SCREEN_H);

        const uint32_t tint = lf(registry, bg, BG_LAYER_TINT, i);
        if (tint) {
            fill_layer(registry, bg, i, tint);
            continue;
        }

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
        const uint32_t transparent = lf(registry, bg, BG_LAYER_TRANSPARENCY, i);
        const int32_t y = (int32_t)lf(registry, bg, BG_LAYER_Y, i);
        const int32_t lx = (int32_t)lf(registry, bg, BG_LAYER_X, i);
        const int32_t loop = (int32_t)lf(registry, bg, BG_LAYER_LOOP, i);

        /* LF2 keeps only IDIV's integer answer. At a magnified output that turns a rational
         * scroll into visible stalls and whole-logical-pixel jumps, so retain the discarded
         * fraction for this layer's complete synchronous draw, including continuations. */
        render_background_phase_set(
            geom_layer_offset_phase(span, stage_width, camera, view));
        if (loop > 0) {
            /* The game stops at the layer's span, which is exactly enough to fill 794. A
             * wider view needs more copies, and a LOOPING layer is the one kind that can
             * honestly provide them: the repeat step is the layer's own declared `loop:`,
             * so carrying it on is the stage's layout continued rather than invented. At
             * the game's own 794 this adds nothing -- a layer's span always reaches the
             * right edge there, which is why the native-width arm of the test still comes
             * out byte-identical. */
            for (int32_t x = lx; x < span || off + x < view; x += loop)
                draw_layer(obj, off + x, y, transparent, arg0);
        } else {
            /* loop < 0 would spin for ever; the game would too, but it is a data error and
             * drawing the layer once is the closest thing to what was meant. */
            /* The keyed mountain pieces are not tiles or separate canvases: repeating or
             * independently centring them breaks their baked registration. Every layer keeps
             * the game's shared origin. Only the declared opaque far plane continues behind it. */
            int translation, backdrop_flags;
            backdrop_plane_placement(stage_name, span, lx, view, &translation, &backdrop_flags);
            if (transparent) backdrop_flags &= ~BACKDROP_EXTEND_BOTTOM;
            if (trace_selected)
                fprintf(stderr,
                        "backdrop layer frame %ld index=%d span=%d x=%d off=%d flags=%d\n",
                        trace_frame, i, span, lx, off + translation, backdrop_flags);
            world_backdrop_hint_set(backdrop_flags);
            draw_layer(obj, off + lx + translation, y, transparent, arg0);
            world_backdrop_hint_set(0);
        }
        render_background_phase_set(0.0f);
    }
    /* Anything nearer than every layer -- still behind the sprites, which the game goes on
     * placing itself. */
    while (next_gap < geom_ngaps)
        geom_submit(geom_gaps[next_gap++], (int)camera, (int)view, GEOM_SCREEN_H);

    R(ESP) += 8;                                     /* RET 4: return address and one arg */
}
