/* The port's pure geometry: the parts of it that are functions of NUMBERS ONLY.
 *
 * WHY THIS FILE EXISTS. Nearly every behavioural claim this port makes about widescreen, the
 * camera, the menus and the audio was being proved by booting the game, driving it through
 * its menus for three thousand frames and grepping the log. Those runs take minutes each, and
 * a test that takes minutes is a test nobody runs while they are working -- which is how the
 * mouse route sat green and broken for as long as it existed.
 *
 * But almost none of those claims actually need the game. "Where does the audible span end at
 * a 1920 view", "which overlay row is y 87 in", "where does the camera stop when the section
 * lock is 106 and the view is 1100" are arithmetic. They only needed a running game because
 * the arithmetic was buried inside functions that also touched guest memory.
 *
 * So the arithmetic lives here, `static inline`, and is included BY the overrides that use it
 * and BY tests/test_geom.c. Not copied into the test -- included. A test carrying its own
 * copy of the implementation proves the copy works.
 *
 * WHAT BELONGS HERE: a function whose inputs are all numbers the caller already has. What
 * does NOT: anything that reads the guest, the window, the clock or a file. Those stay in
 * their override and take these as helpers, so this header never needs a runtime to compile.
 */
#ifndef LF2_GEOM_H
#define LF2_GEOM_H

#include <stdint.h>

/* The game's own screen. Every constant below is expressed against it, because that is how
 * the game expressed them. */
enum {
    GEOM_SCREEN_W = 794,
    GEOM_SCREEN_H = 550,
    /* Stage art stops above the 22-row black caption strip where the game draws
     * "Stage mode (Difficult)". A backdrop extension belongs behind the world, not behind
     * that screen furniture. The stage blit trace pins the split at y=528. */
    GEOM_WORLD_BOTTOM = 528,
};

/* ---- HOW BIG THE WORLD IS DRAWN, AND HOW MUCH OF IT (issue #41) ----
 *
 * Two different questions, and the port answered only the second for a while. SCALE is how
 * many screen pixels a game pixel becomes; FIELD OF VIEW is how much world is on screen. The
 * height fixes the scale and the width spends what is left over on field of view:
 *
 *     scale = min(win_h / 550, win_w / 794)     -- the picture FILLS the window
 *     view  = win_w / scale, floored at 794     -- whatever width is left is more world
 *
 * WHY THE HEIGHT FIXES THE SCALE AND NOT THE WIDTH. LF2's vertical screen axis carries z and
 * jump height, both fixed by the stage's own data, and every background layer's picture is
 * 550 rows tall -- so there is no more world above or below, at any window size. The rows
 * that exist have to fill the window's rows, which is a scale. Horizontally there IS more
 * world, so extra width is given as world rather than as magnification. The `min` is what
 * happens when a window is proportionally NARROWER than the game: the width binds instead,
 * the view floors at the game's own 794, and the leftover rows are black bands. There is
 * nothing to put in them.
 *
 * THIS IS NOT THE SCALE THE PORT REMOVED. That one composed a small frame and let SDL blow
 * the finished picture up, so every game pixel became a 2x2 block and text and lighting were
 * quantised to the small grid before being enlarged. This scale is applied PER QUAD as the
 * display list is drawn into a full-resolution target (runtime/video/render.c): the geometry stays
 * exact at float precision and only a sprite's own texels are magnified. The distinction is
 * the whole of issue #41 and it is why this is a renderer property, not a presentation one.
 *
 * A fractional scale is allowed and expected -- 1080/550 is 1.963. Rounding it to a whole
 * number would leave a 1080-row window with 530 rows of black, which is the thing being fixed.
 */
static inline float geom_world_scale(int win_w, int win_h)
{
    if (win_w <= 0 || win_h <= 0) return 1.0f;
    const float sh = (float)win_h / (float)GEOM_SCREEN_H;
    const float sw = (float)win_w / (float)GEOM_SCREEN_W;
    return sh < sw ? sh : sw;
}

/* The composition's width: how much WORLD is on screen, in the game's own pixels. Floored at
 * the game's 794 (below that the HUD strip does not fit) and capped at what the build
 * allocated surface pitch for.
 *
 * The width is CEILED from the aspect rather than rounded to nearest. The composition is an
 * integer-sized game surface while the ideal view generally is not: at 3840x1975 it is
 * 1069.367 game pixels. Rounding that down to 1069 makes its uniformly-scaled right edge stop
 * short of the drawable and exposes a column of the cleared target. One extra world column
 * can be clipped symmetrically; a missing one cannot cover the output. Use integer aspect
 * arithmetic so an exactly integral view is not accidentally ceiled by floating-point noise. */
static inline int geom_compose_width(int win_w, int win_h, int wide_max)
{
    if (win_w <= 0 || win_h <= 0) return GEOM_SCREEN_W;
    const long long numerator = (long long)win_w * GEOM_SCREEN_H;
    long long w = (numerator + win_h - 1) / win_h;
    if (w < GEOM_SCREEN_W) w = GEOM_SCREEN_W;
    return w > wide_max ? wide_max : (int)w;
}

/* Where the scaled composition sits in the window, as a destination rectangle. Both present
 * paths need it and neither can work it out alone -- one knows the composition, the other the
 * output -- so it is one function. Centred on both axes; a leftover band is black.
 *
 * Negative x is correct and deliberate: a window the composition cannot be squeezed into
 * (because the view floored at 794) shows the MIDDLE of the picture rather than a squashed
 * whole one. */
static inline void geom_compose_rect(int win_w, int win_h, int comp_w, int comp_h,
                                     float *x, float *y, float *w, float *h)
{
    const float s = geom_world_scale(win_w, win_h);
    const float dw = (float)comp_w * s, dh = (float)comp_h * s;
    *w = dw; *h = dh;
    *x = ((float)win_w - dw) * 0.5f;
    *y = ((float)win_h - dh) * 0.5f;
}

/* A POINT ON THE WINDOW, BACK IN THE GAME'S OWN PIXELS. The exact inverse of the rectangle
 * above, and it must stay exact: every mouse hit test in the port goes through it, and when it
 * disagrees with the placement nothing looks wrong -- a menu just activates the wrong entry,
 * or none. That silence is why the round trip is asserted offline rather than trusted.
 *
 * The old mapping subtracted a horizontal centring offset and nothing else, which was already
 * wrong VERTICALLY in a tall window (the picture was centred with 265 rows above it at 1080
 * and the pointer's y was never moved to match) and would have been wrong in both axes the
 * moment a scale existed. */
static inline void geom_window_to_compose(int win_w, int win_h, int comp_w, int comp_h,
                                          float wx, float wy, float *cx, float *cy)
{
    float rx, ry, rw, rh;
    geom_compose_rect(win_w, win_h, comp_w, comp_h, &rx, &ry, &rw, &rh);
    const float s = geom_world_scale(win_w, win_h);
    *cx = (wx - rx) / s;
    *cy = (wy - ry) / s;
}

/* ---- HOW FAR AWAY A BACKGROUND LAYER IS, WHICH THE GAME ALREADY SAYS (issues #49, #62) ----
 *
 * The hand-woven stage sets need a depth for everything in them, and the assumption behind
 * issue #62 was that all of it would have to be authored. For the layers the game already
 * draws, it does not: the depth is in the shipped data and has been all along.
 *
 * THE DERIVATION, from the parallax the port already implements. fn_0041a250 offsets a layer by
 *
 *     off = -((span - 794) * camera) / (stage_width - 794)
 *
 * so a layer moves at RATE r = (span - 794) / (stage_width - 794) as the camera pans: r = 0 is
 * a layer that never moves, r = 1 is one that moves exactly with the camera. That is a
 * perspective divide written as a scroll ratio. For a camera translating past a point at depth
 * z, the point's screen shift goes as 1/z, so a layer at rate r sits at
 *
 *     z / z_ref = 1 / r
 *
 * where z_ref is the plane that moves 1:1 with the camera -- the plane the FIGHTERS stand in,
 * since the game pans to keep them centred. Every number in it is already read by
 * runtime/overrides/background.c.
 *
 * WHY THIS IS AN IDENTIFICATION AND NOT AN ANALOGY, and it is worth stating because a plausible
 * formula over two numbers is cheap. Applied to all 12 shipped stages it produces a depth
 * ordering that matches each stage's own DRAWING ORDER, which nothing forced it to: Tai Hom
 * Village comes out 134, 17.5, 13.9, 1.75, 1.45, 1.33, 1.11, 1.00 in file order; CUHK puts its
 * sky at 4.66, its buildings at 2.1-2.6 and its front floor at 1.00. And it predicts something
 * no ordering could have suggested -- The Great Wall's `road3` has rate 1.125, i.e. z 0.89,
 * IN FRONT of the fighters -- which is exactly what that layer is: the strip along the bottom
 * of the screen at y 481.
 *
 * THE TWO DEGENERATE CASES ARE REAL STAGES, not guards invented for the arithmetic:
 *   stage_width <= 794   HK Coliseum. There is no camera pan at all, so no layer's rate is
 *                        observable and no depth can be derived. Returns 0, meaning "unknown".
 *   span <= 794          a layer that never moves: infinitely far. Returns 0 as well, and the
 *                        caller must place it at its own far plane rather than at 1/0.
 * A caller that treats 0 as "at the fighters' plane" would put every stage's sky in the fight.
 */
static inline float geom_layer_depth(int span, int stage_width)
{
    if (stage_width <= GEOM_SCREEN_W) return 0.0f;   /* no pan: nothing is observable */
    const float r = (float)(span - GEOM_SCREEN_W) / (float)(stage_width - GEOM_SCREEN_W);
    if (r <= 0.0f) return 0.0f;                      /* never moves: infinitely far */
    return 1.0f / r;
}

/* ---- WHERE A POINT IN THE STAGE LANDS ON THE SCREEN (issues #49, #62) ----
 *
 * The projection the hand-woven geometry is drawn with, and it is the GAME'S, derived rather
 * than chosen. Three facts settle it, all of them already recorded:
 *
 *   C031  a layer at parallax depth d shifts by camera/d -- so the horizontal is a 1/z
 *         translation.
 *   C018  a fighter's z (+0x18) is used DIRECTLY as a screen row: runtime/overrides/objects.c
 *         draws its shadow at [o+0x18] - h/2 and its tags at [o+0x18] + 3. So the depth axis
 *         projects down the screen at slope exactly 1, and that slope IS the camera's tilt.
 *         Jump height (+0x14) subtracts from it.
 *   ----  and every object shifts by the camera FLAT, whatever its z. The game gives a fighter
 *         at the near zboundary and one at the far the same parallax rate.
 *
 * The last of those is why this is not a perspective camera and cannot be made into one: a
 * perspective camera must give those two fighters different rates, and the game does not. The
 * projection magnifies by exactly 1 at every depth -- LF2 draws every layer's picture at its
 * authored size no matter how far away it is.
 *
 * WHICH IS WHY IT IS NOT A 4x4 MATRIX EITHER, and that is worth stating because the instinct is
 * to write one. screen_x = X - camera/d is not a linear function of (X, d, 1): a matrix with a
 * perspective divide gives X/d, and this needs X - c/d. So the depth is carried PER VERTEX and
 * the division happens per vertex. runtime/shaders/mesh.vert is a transcription of the two
 * lines below and says so; this is the copy under test.
 *
 * FOUR NUMBERS, NOT THREE. x, jump height, floor row and parallax depth are independent in LF2
 * because the game never unified them -- see issue #62. A caller that collapses `row` and
 * `depth` into one axis is authoring against a camera the game does not have.
 *
 * A depth of 0 means UNKNOWN (geom_layer_depth's answer for a stage that cannot pan, and for a
 * layer that never moves). It is treated as infinitely far -- no camera shift at all -- which
 * is what a layer that never moves does. Reading it as "at the fighters' plane" would make
 * every stage's sky pan with the fight.
 */
static inline void geom_stage_project(int camera, float x, float jump, float row, float depth,
                                      float *sx, float *sy)
{
    *sx = x - ((depth > 0.0f) ? (float)camera / depth : 0.0f);
    *sy = row - jump;
}

/* The same point in clip space, which is what the vertex shader needs. The view is the
 * composition's width and the game's own 550 rows; z is the depth mapped monotonically into
 * [0,1] so the depth test orders by it, nearer being smaller.
 *
 * depth/(depth+1) rather than a near/far plane: the shipped stages run from 0.89 (The Great
 * Wall's road3, in FRONT of the fighters) to 535 (Forbidden Tower's sky), which no fixed pair
 * of planes covers without wasting most of the buffer's precision on emptiness. This puts the
 * fighters' plane at exactly 0.5 and keeps resolution where the geometry actually is. */
static inline void geom_stage_clip(int camera, int view_w, int view_h,
                                   float x, float jump, float row, float depth,
                                   float *cx, float *cy, float *cz)
{
    float sx = 0, sy = 0;
    geom_stage_project(camera, x, jump, row, depth, &sx, &sy);
    *cx = (view_w > 0) ? (2.0f * sx / (float)view_w - 1.0f) : 0.0f;
    *cy = (view_h > 0) ? (1.0f - 2.0f * sy / (float)view_h) : 0.0f;
    *cz = (depth > 0.0f) ? depth / (depth + 1.0f) : 1.0f;   /* unknown depth: the far plane */
}

/* ---- A SCALED DISPLAY, AND THE POINTER ON ONE (issue #56) ----
 *
 * SDL sizes a window in POINTS and draws it in PIXELS, and on a HiDPI display those differ by
 * the window's pixel density -- 1920x1080 points is a 3840x2160 drawable at 200%. Everything
 * above takes the PIXEL size, because that is what the frame is drawn into. The pointer does
 * not: SDL delivers it in POINTS. So the one place the density enters the port's geometry is
 * here, and it is a multiply rather than a special case, because the density is 1.0 wherever
 * this does not apply.
 *
 * WHAT MUST BE TRUE, and what tests/test_geom.c walks: a scaled display changes the RESOLUTION
 * the picture is drawn at and NOTHING ELSE. The same fraction of the window is the same
 * composition pixel, and the composition is the same width, at every density. That is the
 * whole meaning of "not upscaled" -- a port that composed from the point size would pass a
 * round-trip test just as happily while drawing a 1080p frame onto a 4K panel, so the
 * invariant is stated across densities, not within one. */
static inline void geom_pointer_to_compose(int pix_w, int pix_h, int comp_w, int comp_h,
                                           float density, float px, float py,
                                           float *cx, float *cy)
{
    if (!(density > 0.0f)) density = 1.0f;
    geom_window_to_compose(pix_w, pix_h, comp_w, comp_h, px * density, py * density, cx, cy);
}

/* ---- WHERE A FIXED-794 SCREEN SITS IN A WIDER COMPOSITION (issue #44) ----
 *
 * The front end, the mode menu, the loading screen, character selection and the pre-fight
 * overlay are all authored 794 wide and cannot be made wider: there is no more of them. On a
 * wider composition each one therefore needs a horizontal placement, and the port had exactly
 * ONE answer for all of them -- centre.
 *
 * EVERY SCREEN IS CENTRED, including the two menus -- their MENU itself, its logo and its list,
 * sit in the middle of the window like everything else. What is special about those two is only
 * their BACKDROP ART: the character portrait (MENU_BACK<n>, 257-409 px wide and 546 of the
 * screen's 550 rows tall) is drawn at a hard literal x = 0 and bleeds off the LEFT EDGE, so it
 * keeps that edge while the menu in front of it is centred. That exception is applied where the
 * draw happens (runtime/video/ddraw.c, backdrop_art), not here.
 *
 * An earlier attempt left-aligned the WHOLE screen, which dragged the logo and the menu list to
 * the edge with the picture. GEOM_ALIGN_LEFT survives because the arithmetic of "anchored at
 * x 0" is worth having in one place and tested, but nothing asks for it screen-wide any more.
 *
 * This is only the arithmetic, so that the four consumers of the offset share one definition of
 * it and tests/test_geom.c can walk it without booting the game. */
enum { GEOM_ALIGN_CENTRE = 0, GEOM_ALIGN_LEFT = 1 };

static inline int geom_screen_offset_x(int comp_w, int align)
{
    if (comp_w <= GEOM_SCREEN_W) return 0;      /* nothing spare: there is nowhere to move */
    if (align == GEOM_ALIGN_LEFT) return 0;
    return (comp_w - GEOM_SCREEN_W) / 2;
}

/* A fixed item inside the live world is not necessarily centred inside the original screen.
 * Preserve the authored native placement, but centre the item's own bounds once the
 * composition is wider. */
static inline int geom_item_offset_x(int comp_w, int item_left, int item_w)
{
    if (comp_w <= GEOM_SCREEN_W) return 0;
    return (comp_w - item_w) / 2 - item_left;
}

/* ---- the stage's parallax (runtime/overrides/background.c) ----
 *
 * The game's own order of operations: the product first, then the divide, then the negate.
 * Rearranging changes the rounding -- x86 IDIV and C both truncate toward zero -- and that is
 * a different picture, one pixel at a time. A layer with less picture than the view is wide
 * cannot be scrolled to cover it, and the formula run past that point inverts. */
static inline int64_t geom_layer_scroll_product(int span, int camera, int view)
{
    return (int64_t)(span - view) * (int64_t)camera;
}

static inline int geom_layer_offset(int span, int stage_width, int camera, int view)
{
    if (stage_width <= view || span <= view) return 0;
    const int64_t divisor = (int64_t)stage_width - (int64_t)view;
    return (int)(-(geom_layer_scroll_product(span, camera, view) / divisor));
}

/* LF2's IDIV answer above is the correct native 1x coordinate, but it discards a rational
 * remainder that a magnified per-draw raster can represent. Return that signed fraction so
 * both native renderers can retain the spatial precision without changing the guest value. */
static inline float geom_layer_offset_phase(int span, int stage_width, int camera, int view)
{
    if (stage_width <= view || span <= view) return 0.0f;
    const int64_t divisor = (int64_t)stage_width - (int64_t)view;
    return -(float)(geom_layer_scroll_product(span, camera, view) % divisor) / (float)divisor;
}

/* ---- where the camera may stop (runtime/overrides/background.c) ----
 *
 * Two bounds, and the game states both against a 794-wide screen: the stage's own end, and
 * the stage-mode SECTION LOCK at 0x00450bb0 which holds the camera until a section is cleared
 * (zero in VS mode). Both mean "the right edge of the screen goes HERE", so both take the same
 * 794 -> view substitution; at view 794 this is exactly the game's own answer. */
static inline int geom_camera_max(int stage_width, int view, int lock)
{
    int max = stage_width - view;
    if (lock) {
        const int lock_max = lock + GEOM_SCREEN_W - view;
        if (lock_max < max) max = lock_max;
    }
    return max < 0 ? 0 : max;
}

/* ---- where a FIGHTER may walk (runtime/overrides/background.c) ----
 *
 * THE GAME'S OWN INVARIANT, and this is a port of it rather than a constant that makes one
 * screenshot look right. A stage-mode section is written into two words from ONE stage-data
 * field B (fn_00437860 at 0x00437b25/0x00437b38, claim C024):
 *
 *     [0x00450bb0]  the CAMERA lock, B - 794      "put the screen's LEFT edge here"
 *     [0x00450bb4]  the WALK lock,   B            "a fighter may walk to here"
 *
 * Read together they say ONE thing: when the camera is at its bound, the walkable area ends
 * exactly at the RIGHT EDGE OF THE SCREEN. That is the invariant, and the 794 in the first
 * word is the only place the screen appears -- which is why issue #36 could substitute the
 * view there and why the walk bound has nothing to substitute.
 *
 * IT BREAKS WHEN THE CAMERA CANNOT REACH ITS BOUND. B - view goes negative near a stage's
 * start, the camera clamps at 0, and the screen's right edge lands at `view` while a fighter
 * still stops at B. Measured in stage 1-1's first section: B = 900, view = 978, camera 0 --
 * seventy-eight world pixels of stage visible that cannot be walked to (issue #43).
 *
 * So the walk bound follows the same rule the camera bound does: the screen's right edge.
 * `camera_max + view` IS that edge, in world x.
 *
 * WHY THE GUARD IS NOT BELT-AND-BRACES. At view == 794 this returns the game's own B
 * untouched, by construction rather than by arithmetic that happens to agree -- and it has to,
 * because the game has this same situation at 4:3 whenever a section's B is under 794, and
 * matching the game there is not optional. tools/e2e.py background's byte-identity arm is what
 * would catch it. */
static inline int geom_walk_max(int walk, int camera_max, int view)
{
    if (view <= GEOM_SCREEN_W) return walk;     /* the game's own answer, unaltered */
    const int edge = camera_max + view;         /* the screen's right edge, in world x */
    return edge > walk ? edge : walk;
}

/* The camera the WORLD IS DRAWN FROM: the game's, shifted left by half the extra width so a
 * wider view is CENTRED on what the 4:3 view showed rather than extended to the right. Clamped
 * at zero because there is no world left of the stage's start. */
static inline int geom_draw_camera(int camera, int view)
{
    const int k = (view - GEOM_SCREEN_W) / 2;
    if (k <= 0) return camera;
    const int c = camera - k;
    return c > 0 ? c : 0;
}

/* ---- the pre-fight overlay's rows (runtime/overrides/screens.c) ----
 *
 * THE GAME'S OWN, DECOMPILED. Ghidra on FUN_00429730 -- the only function that touches
 * OVERLAY_SEL -- draws the highlight for item i at these y, and they are NOT a uniform step:
 *
 *     0 -> (0x5c, 0x10)   1 -> (0x40, 0x27)   2 -> (0x28, 0x40)
 *     3 -> (0x0f, 0x57)   4 -> (0x25, 0x6f)   5 -> (0x65, 0x89)
 *
 * The port used to assume 24 px from 16, measured off three sampled highlight blits -- and
 * items 0, 2 and 5 are exactly the three a uniform step gets nearly right, so the method that
 * produced it could never have shown the error. Claim C022.
 */
enum { GEOM_OVERLAY_ITEMS = 6, GEOM_OV_X0 = 3, GEOM_OV_X1 = 307 };

static const int GEOM_OV_ROW_Y[GEOM_OVERLAY_ITEMS + 1] = { 16, 39, 64, 87, 111, 137, 163 };

/* The x band is the whole panel rather than each label's own left edge: the list is drawn on
 * a slant, and a player aiming at it should not have to hit the glyphs. The y alone identifies
 * the row unambiguously. */
static inline int geom_overlay_item_at(int x, int y)
{
    if (x < GEOM_OV_X0 || x > GEOM_OV_X1) return -1;
    if (y < GEOM_OV_ROW_Y[0] || y >= GEOM_OV_ROW_Y[GEOM_OVERLAY_ITEMS]) return -1;
    for (int i = 0; i < GEOM_OVERLAY_ITEMS; i++)
        if (y < GEOM_OV_ROW_Y[i + 1]) return i;
    return -1;
}

/* ---- the stereo pan (runtime/overrides/audio_pan.c) ----
 *
 * Two speakers placed on the SCREEN at x 200 and 600, each at full volume within 200 px and
 * fading to nothing at 400 -- the 794 screen's quarter points, written down as pixels. The
 * audible span is therefore -200..1000, wider than the game's own picture, which is why
 * nothing is culled at 794 and why the function had never been looked at.
 *
 * Scaled by view/794 rather than re-derived from the view: `view/4` would give 198 at the
 * native width instead of the 200 that shipped, and changing a game nobody asked to change is
 * not a fix. At view 794 every constant is bit-for-bit what it was.
 */
enum { GEOM_PAN_LEFT_X = 200, GEOM_PAN_RIGHT_X = 600,
       GEOM_PAN_NEAR = 200, GEOM_PAN_FAR = 400, GEOM_PAN_FULL = 100 };

static inline int geom_pan_scaled(int px, int view)
{
    return (int)(((long)px * (long)view) / (long)GEOM_SCREEN_W);
}

static inline int geom_pan_falloff(int sx, int centre, int near, int far)
{
    int d = sx - centre;
    if (d < 0) d = -d;
    if (d < near) return GEOM_PAN_FULL;
    if (d >= far) return 0;
    return ((far - d) * GEOM_PAN_FULL) / (far - near);
}

/* The two ear volumes for a sound at screen x `sx`, in a view `view` wide. */
static inline void geom_pan(int sx, int view, int *left, int *right)
{
    const int near = geom_pan_scaled(GEOM_PAN_NEAR, view);
    const int far  = geom_pan_scaled(GEOM_PAN_FAR, view);
    *left  = geom_pan_falloff(sx, geom_pan_scaled(GEOM_PAN_LEFT_X, view),  near, far);
    *right = geom_pan_falloff(sx, geom_pan_scaled(GEOM_PAN_RIGHT_X, view), near, far);
}

#endif
