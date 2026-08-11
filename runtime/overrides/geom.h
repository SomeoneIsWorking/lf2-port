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
 * and BY runtime/test_geom.c. Not copied into the test -- included. A test carrying its own
 * copy of the implementation proves the copy works.
 *
 * WHAT BELONGS HERE: a function whose inputs are all integers the caller already has. What
 * does NOT: anything that reads the guest, the window, the clock or a file. Those stay in
 * their override and take these as helpers, so this header never needs a runtime to compile.
 */
#ifndef LF2_GEOM_H
#define LF2_GEOM_H

/* The game's own screen. Every constant below is expressed against it, because that is how
 * the game expressed them. */
enum { GEOM_SCREEN_W = 794, GEOM_SCREEN_H = 550 };

/* ---- the composition, from the window (runtime/ddraw.c) ----
 *
 * The width is the window's REAL pixels and the height is the game's own 550, because LF2's
 * vertical axis carries z and jump height and every layer's picture is 550 rows tall -- there
 * is no more world to show. See hostwin_window_geometry for the whole argument. */
static inline int geom_compose_width(int win_w, int wide_max)
{
    int w = win_w < GEOM_SCREEN_W ? GEOM_SCREEN_W : win_w;
    return w > wide_max ? wide_max : w;
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

/* ---- how big an object is drawn (runtime/render.c) ----
 *
 * The frame is drawn at the window's real pixels, so a fighter would be the ~40 rows the
 * artist drew. Objects are magnified by how many times the game's own screen fits in the
 * window, rounded to a WHOLE number -- whole because these are nearest-neighbour pixel-art
 * sprites and a fractional factor puts some of their pixels down two screen pixels wide and
 * others three. */
static inline int geom_object_scale(int win_h)
{
    int n = (win_h + GEOM_SCREEN_H / 2) / GEOM_SCREEN_H;
    if (n < 1) n = 1;
    return n > 8 ? 8 : n;
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
