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

/* The game's own screen. Every constant below is expressed against it, because that is how
 * the game expressed them. */
enum { GEOM_SCREEN_W = 794, GEOM_SCREEN_H = 550 };

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
 * allocated surface pitch for. */
static inline int geom_compose_width(int win_w, int win_h, int wide_max)
{
    const float s = geom_world_scale(win_w, win_h);
    int w = (int)((float)win_w / s + 0.5f);
    if (w < GEOM_SCREEN_W) w = GEOM_SCREEN_W;
    return w > wide_max ? wide_max : w;
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

/* ---- the stage's parallax (runtime/overrides/background.c) ----
 *
 * The game's own order of operations: the product first, then the divide, then the negate.
 * Rearranging changes the rounding -- x86 IDIV and C both truncate toward zero -- and that is
 * a different picture, one pixel at a time. A layer with less picture than the view is wide
 * cannot be scrolled to cover it, and the formula run past that point inverts. */
static inline int geom_layer_offset(int span, int stage_width, int camera, int view)
{
    if (stage_width <= view || span <= view) return 0;
    return -(((span - view) * camera) / (stage_width - view));
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
