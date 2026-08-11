/* The port's geometry, tested without booting the game.
 *
 * Every claim below used to be proved by launching a headless instance, driving it through
 * three thousand frames of menus and grepping the log -- minutes per claim. None of them
 * needed the game: they are arithmetic, and they only looked like they needed it because the
 * arithmetic was buried in functions that also touched guest memory. runtime/overrides/geom.h
 * is that arithmetic, and this file INCLUDES it rather than carrying a copy, so what is under
 * test is the code that ships.
 *
 * What this cannot cover, stated so the coverage is not overread: whether the game actually
 * calls these with the numbers assumed, and whether a route reaches a screen. Those still need
 * a running game -- the scripts under tools/ -- but they are now the only things that do.
 */
#include "overrides/geom.h"

#include <stdio.h>

static int failures, checks;

static void eq(const char *what, long got, long want)
{
    checks++;
    if (got == want) return;
    failures++;
    printf("  FAIL  %s: got %ld, expected %ld\n", what, got, want);
}

/* The scale is fractional by design (1080/550 = 1.963...), so its checks compare floats.
 * The tolerance is a quarter of a screen pixel at 4K rather than an epsilon: what matters is
 * that a window's rows are filled to within less than a pixel, and an exact-equality test on
 * a computed float would be a test of the compiler's rounding, not of the geometry. */
static void eqf(const char *what, float got, float want)
{
    checks++;
    const float d = got - want;
    if (d > -0.25f && d < 0.25f) return;
    failures++;
    printf("  FAIL  %s: got %.4f, expected %.4f\n", what, (double)got, (double)want);
}

/* ---- the world FILLS the window: scale from the height, field of view from the width ---- */
static void test_scale(void)
{
    /* The game's own window is 1:1 exactly. This is the property that keeps every byte-identity
     * arm true -- tools/e2e.sh background and tools/e2e.sh render both run at 794x550, and a
     * scale of anything but exactly 1 there would change every pixel of both. */
    eqf("794x550 is exactly 1:1", geom_world_scale(794, 550), 1.0f);
    /* The height binds whenever the window is wider in aspect than the game. 1080/550. */
    eqf("1920x1080 scales by the height", geom_world_scale(1920, 1080), 1080.0f / 550.0f);
    eqf("1600x550 still 1:1 (only the width grew)", geom_world_scale(1600, 550), 1.0f);
    /* And the width binds when the window is proportionally NARROWER: 800/794, not 900/550. */
    eqf("800x900: the width binds", geom_world_scale(800, 900), 800.0f / 794.0f);
    /* Below the game's own size it shrinks rather than cropping. */
    eqf("a small window scales down", geom_world_scale(397, 275), 0.5f);

    /* THE PICTURE FILLS THE WINDOW. This is the assertion issue #41 exists for: at every
     * window whose aspect is at least the game's, the drawn height IS the window's height, so
     * there are no black bands. The old design put 265 rows of black above and below a 1080p
     * window and that is what this now forbids. */
    const int win[][2] = { {794,550}, {1600,550}, {1920,1080}, {1280,720}, {2560,1440} };
    for (unsigned i = 0; i < sizeof win / sizeof win[0]; i++) {
        float x, y, w, h;
        const int cw = geom_compose_width(win[i][0], win[i][1], 4096);
        geom_compose_rect(win[i][0], win[i][1], cw, GEOM_SCREEN_H, &x, &y, &w, &h);
        char what[80];
        snprintf(what, sizeof what, "%dx%d: the drawn height fills the window",
                 win[i][0], win[i][1]);
        eqf(what, h, (float)win[i][1]);
        snprintf(what, sizeof what, "%dx%d: no black band above", win[i][0], win[i][1]);
        eqf(what, y, 0.0f);
    }

    /* The narrower-than-the-game case, which is where a band is CORRECT: the width binds, the
     * view floors at 794, and the rows the game has no world for stay black. Stated as its own
     * case so "fills the window" above is not read as a promise it cannot keep. */
    {
        float x, y, w, h;
        const int cw = geom_compose_width(800, 900, 4096);
        eq("800x900 floors the view at 794", cw, 794);
        geom_compose_rect(800, 900, cw, GEOM_SCREEN_H, &x, &y, &w, &h);
        eqf("800x900 fills the WIDTH", w, 800.0f);
        eqf("800x900 is centred vertically with a band", y, (900.0f - 550.0f * (800.0f / 794.0f)) * 0.5f);
    }
}

/* ---- where a fixed-794 screen sits in a wider composition (issue #44) ---- */
static void test_screen_align(void)
{
    /* CENTRED is the old behaviour and must be exactly what it was, or every screen that is
     * not one of the two menus moves. 2542 is the composition of the reported 1710x370 window. */
    eq("centred in 2542", geom_screen_offset_x(2542, GEOM_ALIGN_CENTRE), (2542 - 794) / 2);
    eq("centred in 1600", geom_screen_offset_x(1600, GEOM_ALIGN_CENTRE), (1600 - 794) / 2);
    /* LEFT is zero BY DEFINITION -- the screen's own x 0 is the composition's x 0. This is the
     * whole of the change: the two menus' character portrait is drawn at a hard literal x = 0
     * and hangs on the screen's left edge, so centring moved the anchor into the middle. */
    eq("left in 2542", geom_screen_offset_x(2542, GEOM_ALIGN_LEFT), 0);
    eq("left in 1600", geom_screen_offset_x(1600, GEOM_ALIGN_LEFT), 0);
    /* At the game's own width there is nowhere to move, so the ALIGNMENT CANNOT MATTER. That
     * is what keeps every byte-identity arm true at 794x550 whichever screen is up, and it is
     * the property to check rather than to assume. */
    eq("794 centred is 0", geom_screen_offset_x(794, GEOM_ALIGN_CENTRE), 0);
    eq("794 left is 0",    geom_screen_offset_x(794, GEOM_ALIGN_LEFT), 0);
    eq("narrower than the game is 0",
       geom_screen_offset_x(700, GEOM_ALIGN_CENTRE), 0);
    /* And the two answers only ever differ where there IS width to spare. */
    eq("the two alignments agree at 794",
       geom_screen_offset_x(794, GEOM_ALIGN_CENTRE) == geom_screen_offset_x(794, GEOM_ALIGN_LEFT), 1);
    eq("the two alignments differ at 2542",
       geom_screen_offset_x(2542, GEOM_ALIGN_CENTRE) != geom_screen_offset_x(2542, GEOM_ALIGN_LEFT), 1);
}

/* ---- the pointer maps back to exactly where the picture was drawn ---- */
static void test_unproject(void)
{
    /* THE ROUND TRIP, at every window in the table. A game point projected into the window and
     * mapped back must land on itself: if these two ever disagree the picture is drawn in one
     * place and clicked in another, and NOTHING on screen says so -- the menu simply activates
     * the wrong entry. That is why this is asserted rather than assumed.
     *
     * Walked over the corners and the middle of the composition, not one sample, because an
     * error in the offset and an error in the scale look identical at the centre. */
    const int win[][2] = { {794,550}, {1600,550}, {1920,1080}, {800,900}, {1280,720} };
    for (unsigned i = 0; i < sizeof win / sizeof win[0]; i++) {
        const int ww = win[i][0], wh = win[i][1];
        const int cw = geom_compose_width(ww, wh, 4096);
        const float s = geom_world_scale(ww, wh);
        float rx, ry, rw, rh;
        geom_compose_rect(ww, wh, cw, GEOM_SCREEN_H, &rx, &ry, &rw, &rh);
        const int pts[][2] = { {0,0}, {cw,0}, {0,GEOM_SCREEN_H}, {cw,GEOM_SCREEN_H},
                               {cw/2,GEOM_SCREEN_H/2}, {3,16} };
        for (unsigned j = 0; j < sizeof pts / sizeof pts[0]; j++) {
            /* project by hand, exactly as draw_list does: game point -> window pixel */
            const float sx = (float)pts[j][0] * s + rx;
            const float sy = (float)pts[j][1] * s + ry;
            float bx = 0, by = 0;
            geom_window_to_compose(ww, wh, cw, GEOM_SCREEN_H, sx, sy, &bx, &by);
            char what[96];
            snprintf(what, sizeof what, "%dx%d: (%d,%d) survives the round trip in x",
                     ww, wh, pts[j][0], pts[j][1]);
            eqf(what, bx, (float)pts[j][0]);
            snprintf(what, sizeof what, "%dx%d: (%d,%d) survives the round trip in y",
                     ww, wh, pts[j][0], pts[j][1]);
            eqf(what, by, (float)pts[j][1]);
        }
    }

    /* THE BUG THIS EXISTS FOR, as a negative. The old mapping was "subtract the horizontal
     * centring, leave y alone" -- it passed the pointer's WINDOW row straight through as a
     * game row. Halfway down a 1080-row window is game row 275 of 550; passing 540 through
     * unchanged lands 265 rows lower, which is the bottom edge of the screen. A click aimed at
     * a fighter's head would land at its feet, and nothing anywhere would report it. */
    {
        float bx = 0, by = 0;
        geom_window_to_compose(1920, 1080, 978, GEOM_SCREEN_H, 100.0f, 540.0f, &bx, &by);
        eqf("1920x1080: the middle of the window is the middle of the 550 rows", by, 275.0f);
        eq("passing the window row through unchanged would be 265 rows out",
           (long)(540 - 275), 265);
        /* And it would run off the bottom for anything in the lower half of the window: row
         * 900 of 1080 is game row 458, but passed through it is 900 -- past the screen. */
        geom_window_to_compose(1920, 1080, 978, GEOM_SCREEN_H, 100.0f, 900.0f, &bx, &by);
        eqf("1920x1080: window row 900 is game row 458", by, 900.0f * 550.0f / 1080.0f);
        eq("passed through unchanged it would be off the 550-row screen", 900 > GEOM_SCREEN_H, 1);
    }
}

/* ---- how much world is on screen ---- */
static void test_compose(void)
{
    enum { WIDE_MAX = 4096 };
    eq("compose 794x550 -> 794",   geom_compose_width(794, 550, WIDE_MAX), 794);
    /* A window the game's own height and twice as wide is twice the WORLD at 1:1. */
    eq("compose 1600x550 -> 1600", geom_compose_width(1600, 550, WIDE_MAX), 1600);
    /* 1920x1080: the height scales by 1.963, so the world on screen is 1920/1.963 = 978.
     * That is LESS world than the 1920 this returned before issue #41, and deliberately so --
     * the extra pixels are being spent making the picture fill the window instead. */
    eq("compose 1920x1080 -> 978", geom_compose_width(1920, 1080, WIDE_MAX), 978);
    eq("compose 1280x720 -> 978",  geom_compose_width(1280, 720, WIDE_MAX), 978);
    /* Same aspect, same world, whatever the monitor: that is what a scale buys. */
    eq("compose 2560x1440 -> 978", geom_compose_width(2560, 1440, WIDE_MAX), 978);
    /* Narrower in aspect than the game: floors at 794 rather than showing less than the HUD. */
    eq("compose 800x900 floors",   geom_compose_width(800, 900, WIDE_MAX), 794);
    eq("compose 400x300 floors",   geom_compose_width(400, 300, WIDE_MAX), 794);
    /* And the build's own pitch limit still caps it. 32:9 at 550 rows asks for 1956. */
    eq("compose 9000x550 clamps",  geom_compose_width(9000, 550, WIDE_MAX), WIDE_MAX);
}

/* ---- the stage's parallax ---- */
static void test_parallax(void)
{
    /* Brokeback Clif: stage 1500, the cliff layers span 1379, the floor spans 1500. */
    eq("floor is 1:1 with the camera", geom_layer_offset(1500, 1500, 300, 794), -300);
    eq("a cliff moves less than the floor",
       geom_layer_offset(1379, 1500, 300, 794), -(((1379 - 794) * 300) / (1500 - 794)));
    /* A layer with no more picture than the view is pinned, not scrolled backwards. */
    eq("a layer narrower than the view is pinned", geom_layer_offset(700, 1500, 300, 794), 0);
    eq("a stage narrower than the view is pinned", geom_layer_offset(1379, 700, 300, 794), 0);
    eq("camera 0 is offset 0", geom_layer_offset(1379, 1500, 0, 794), 0);
}

/* ---- where the camera may stop ---- */
static void test_camera(void)
{
    /* No lock: the stage's own end, in view-width terms. */
    eq("stage bound at 794",  geom_camera_max(1500, 794, 0), 1500 - 794);
    eq("stage bound at 1100", geom_camera_max(1500, 1100, 0), 1500 - 1100);
    eq("a stage narrower than the view floors at 0", geom_camera_max(1500, 1920, 0), 0);

    /* The stage-mode section lock. At the game's own width it IS the lock, unchanged -- which
     * is the property that says this substitution cannot alter the 4:3 game. */
    eq("lock at 794 is the lock itself", geom_camera_max(3200, 794, 900), 900);
    /* Wider: the same right edge, so the camera stops earlier by exactly the extra width. */
    eq("lock at 1100 stops 306 earlier", geom_camera_max(3200, 1100, 900), 900 + 794 - 1100);
    /* And when the view is wider than the section can honour, it pins at 0 rather than going
     * negative -- the same degradation the stage ends get. Measured in play: lock 106. */
    eq("a lock the view cannot honour pins at 0", geom_camera_max(1500, 1100, 106), 0);
    /* The tighter of the two bounds wins, whichever it is. */
    eq("the stage bound wins when it is tighter", geom_camera_max(1000, 794, 900), 1000 - 794);
}

/* ---- centring a wide view ---- */
static void test_draw_camera(void)
{
    /* At the game's own width the offset is zero BY DEFINITION, which is what keeps
     * tools/background_test.sh's byte-identity arm true. */
    eq("794 is the game's own camera", geom_draw_camera(400, 794), 400);
    /* 1100 -> offset 153. Measured in play: the game's camera reached 400, drawn as 247. */
    eq("1100 shifts by half the extra", geom_draw_camera(400, 1100), 247);
    eq("clamped at the stage's start",  geom_draw_camera(100, 1100), 0);
    eq("camera 0 stays 0",              geom_draw_camera(0, 1920), 0);
    /* 1920 -> offset 563. */
    eq("1920 shifts by 563",            geom_draw_camera(1000, 1920), 1000 - 563);
}

/* ---- the pre-fight overlay's rows ---- */
static void test_overlay_rows(void)
{
    /* Each item's OWN drawn y must resolve to that item. This is the Ghidra table checked
     * against the hit test -- if the two ever disagree, a click lands on a row the game is
     * not highlighting and nothing on screen says so. */
    static const int drawn_y[GEOM_OVERLAY_ITEMS] = { 16, 39, 64, 87, 111, 137 };
    for (int i = 0; i < GEOM_OVERLAY_ITEMS; i++) {
        char what[64];
        snprintf(what, sizeof what, "the y the game draws item %d at resolves to %d", i, i);
        eq(what, geom_overlay_item_at(150, drawn_y[i]), i);
    }
    /* The last row of each item, i.e. one before the next one's first. */
    eq("38 is still item 0",  geom_overlay_item_at(150, 38), 0);
    eq("63 is still item 1",  geom_overlay_item_at(150, 63), 1);
    eq("110 is still item 3", geom_overlay_item_at(150, 110), 3);
    eq("162 is still item 5", geom_overlay_item_at(150, 162), 5);
    /* Outside the panel, in both axes. */
    eq("above the first row",  geom_overlay_item_at(150, 15), -1);
    eq("below the last row",   geom_overlay_item_at(150, 163), -1);
    eq("left of the panel",    geom_overlay_item_at(2, 25), -1);
    eq("right of the panel",   geom_overlay_item_at(308, 25), -1);

    /* THE REGRESSION THIS EXISTS FOR. The old uniform 24-px step from 16 put the boundaries at
     * 40, 64, 88, 112, 136. Every y below is a row the game draws one item at and the old
     * formula resolved to another -- which is a click landing on the wrong menu entry. */
    eq("39 is item 1, not 0 (old step said 0)",  geom_overlay_item_at(150, 39), 1);
    eq("87 is item 3, not 2 (old step said 2)",  geom_overlay_item_at(150, 87), 3);
    eq("136 is item 4, not 5 (old step said 5)", geom_overlay_item_at(150, 136), 4);
}

/* ---- the stereo pan ---- */
static void test_pan(void)
{
    int l, r;

    /* At the game's own width the speakers are EXACTLY where the game put them. This is the
     * whole reason the constants are scaled rather than re-derived: view/4 would give 198. */
    eq("left speaker at 794",  geom_pan_scaled(GEOM_PAN_LEFT_X, 794), 200);
    eq("right speaker at 794", geom_pan_scaled(GEOM_PAN_RIGHT_X, 794), 600);
    eq("near radius at 794",   geom_pan_scaled(GEOM_PAN_NEAR, 794), 200);
    eq("far radius at 794",    geom_pan_scaled(GEOM_PAN_FAR, 794), 400);

    /* The falloff's own shape, at the game's numbers. */
    geom_pan(200, 794, &l, &r);  eq("on the left speaker: full left", l, 100);
    geom_pan(600, 794, &l, &r);  eq("on the right speaker: full right", r, 100);
    /* Continuous where the plateau meets the ramp. */
    geom_pan(200 + 200, 794, &l, &r); eq("at the near radius the ramp starts at full", l, 100);
    geom_pan(200 + 300, 794, &l, &r); eq("halfway down the ramp", l, 50);
    geom_pan(200 + 400, 794, &l, &r); eq("at the far radius: silent", l, 0);

    /* THE BUG THIS EXISTS FOR: a sound past screen x 1000 was SILENT in both ears, and at a
     * 1920 view that is the right 48% of the picture. */
    geom_pan(1200, 794, &l, &r);
    eq("unscaled, x 1200 is silent in the left ear",  l, 0);
    eq("unscaled, x 1200 is silent in the right ear", r, 0);
    /* Scaled to a 1920 view, the same place on screen is audible. */
    geom_pan(1200, 1920, &l, &r);
    eq("scaled to 1920, x 1200 is audible", (l > 0 || r > 0), 1);

    /* And the span covers the whole picture at both widths -- the property the test that took
     * 270 seconds was asserting. Walked across the picture rather than reasoned about. */
    for (int view = 794; view <= 1920; view += 1126) {
        int silent = -1;
        for (int x = 0; x < view; x++) {
            geom_pan(x, view, &l, &r);
            if (!l && !r) { silent = x; break; }
        }
        char what[80];
        snprintf(what, sizeof what, "no on-screen x is silent at view %d", view);
        eq(what, silent, -1);
    }
    /* The negative: WITHOUT the scaling, a 1920 view does have silent pixels. If this ever
     * stops being true the checks above are not measuring the fix. */
    {
        int silent = -1;
        const int near = GEOM_PAN_NEAR, far = GEOM_PAN_FAR;
        for (int x = 0; x < 1920; x++) {
            const int ll = geom_pan_falloff(x, GEOM_PAN_LEFT_X,  near, far);
            const int rr = geom_pan_falloff(x, GEOM_PAN_RIGHT_X, near, far);
            if (!ll && !rr) { silent = x; break; }
        }
        /* 999, not the 1000 the reach of the right speaker suggests: the ramp is
         * (400 - d) * 100 / 200 in integers, so d = 399 already truncates to zero. The last
         * step of the falloff is silent on both sides. Worth pinning -- it is the kind of
         * off-by-one that a reimplementation "cleans up" into a float and changes. */
        eq("unscaled at 1920 the picture IS silenced, from x 999", silent, 999);
    }
}

int main(void)
{
    test_scale();
    test_screen_align();
    test_unproject();
    test_compose();
    test_parallax();
    test_camera();
    test_draw_camera();
    test_overlay_rows();
    test_pan();

    printf("geometry: %d checks, %d failure(s)\n", checks, failures);
    if (!checks) {
        printf("  FAIL  no checks ran at all, so this says NOTHING\n");
        return 1;
    }
    return failures ? 1 : 0;
}
