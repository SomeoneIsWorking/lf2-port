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

/* One composition pixel, for the cross-density comparisons below. The drawable is a whole
 * number of pixels, so a density of 1.25 on a 794-point window rounds 992.5 to 992 and the
 * aspect it implies is very slightly not the point window's -- at the far corner that is a
 * third of a game pixel. Asserting a quarter-pixel there would be asserting something the
 * pixel grid cannot deliver; asserting a whole one still catches the reported bug by a factor
 * of two, which is what it is worth having. */
static void eqf1(const char *what, float got, float want)
{
    checks++;
    const float d = got - want;
    if (d > -1.0f && d < 1.0f) return;
    failures++;
    printf("  FAIL  %s: got %.4f, expected %.4f\n", what, (double)got, (double)want);
}

/* ---- A SCALED DISPLAY CHANGES THE RESOLUTION AND NOTHING ELSE (issue #56) ----
 *
 * The report is "on a 4K panel the picture looks like a 1080p frame scaled up". The port's
 * answer is that the composition is seeded from the window's PIXEL size, so a 1920x1080-point
 * window at density 2 composes from 3840x2160 -- the same amount of world, drawn at four times
 * the pixels. That is a claim about arithmetic and it is asserted here, at five densities,
 * because the machine this is developed on has no scaled display and no compositor available
 * on it will report one: SDL's X11 backend reports points == pixels by construction, and a
 * nested kwin at --scale 2 hands its clients a density of 1.00. So the run that would exercise
 * this end to end cannot be made here, and pretending otherwise by reading a density out of an
 * env var would be testing the env var.
 *
 * WHY THE ROUND TRIP ABOVE IS NOT ENOUGH, and this is the whole point of stating the invariant
 * ACROSS densities: a port that composed from the POINT size while drawing into the PIXEL size
 * -- exactly the bug reported -- has a perfectly self-consistent round trip at every density.
 * It is only wrong when compared with a different density, where it gives a different answer
 * for the same fraction of the same window. */
static void test_density(void)
{
    enum { WIDE_MAX = 4096 };
    /* Point sizes a player might have, and the densities a desktop actually offers: 100%,
     * 125%, 150%, 175% (fractional, and the one that catches integer-only arithmetic) and
     * 200%. */
    const int pts[][2] = { {794,550}, {1280,720}, {1920,1080}, {1600,900} };
    const float dens[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        const int pw = pts[i][0], ph = pts[i][1];
        const int base_cw = geom_compose_width(pw, ph, WIDE_MAX);

        for (unsigned k = 0; k < sizeof dens / sizeof dens[0]; k++) {
            const float d = dens[k];
            const int xw = (int)((float)pw * d + 0.5f);   /* the drawable SDL gives us */
            const int xh = (int)((float)ph * d + 0.5f);
            const int cw = geom_compose_width(xw, xh, WIDE_MAX);
            char what[128];

            /* THE FIELD OF VIEW IS THE SAME. If this moved, a HiDPI player would see more or
             * less of the stage than everyone else, which is a gameplay difference. */
            snprintf(what, sizeof what,
                     "%dx%d points at density %.2f composes %d wide, as at density 1",
                     pw, ph, (double)d, base_cw);
            eq(what, cw, base_cw);

            /* THE WORLD SCALE IS THE DENSITY TIMES THE UNSCALED ONE. This is the assertion
             * that the extra pixels became resolution: the same world row is drawn `d` times
             * as tall, which is what "drawn at the panel's resolution" means. */
            snprintf(what, sizeof what, "%dx%d at density %.2f scales the world by %.2fx",
                     pw, ph, (double)d, (double)d);
            eqf(what, geom_world_scale(xw, xh), geom_world_scale(pw, ph) * d);

            /* AND A CLICK LANDS IN THE SAME PLACE. The pointer arrives in POINTS at every
             * density, so the SAME point must give the SAME composition pixel. Walked over
             * the corners and the middle, because an offset error and a scale error agree at
             * the centre. */
            const int probe[][2] = { {0,0}, {pw,0}, {0,ph}, {pw,ph},
                                     {pw/2,ph/2}, {17,ph-3}, {pw-3,11} };
            for (unsigned j = 0; j < sizeof probe / sizeof probe[0]; j++) {
                float bx = 0, by = 0, rx = 0, ry = 0;
                geom_pointer_to_compose(xw, xh, cw, GEOM_SCREEN_H, d,
                                        (float)probe[j][0], (float)probe[j][1], &bx, &by);
                geom_pointer_to_compose(pw, ph, base_cw, GEOM_SCREEN_H, 1.0f,
                                        (float)probe[j][0], (float)probe[j][1], &rx, &ry);
                snprintf(what, sizeof what, "%dx%d density %.2f: point (%d,%d) lands where it "
                         "does unscaled, in x", pw, ph, (double)d, probe[j][0], probe[j][1]);
                eqf1(what, bx, rx);
                snprintf(what, sizeof what, "%dx%d density %.2f: point (%d,%d) lands where it "
                         "does unscaled, in y", pw, ph, (double)d, probe[j][0], probe[j][1]);
                eqf1(what, by, ry);
            }
        }
    }

    /* THE REPORTED BUG, AS A NEGATIVE, so a passing run above means something. Composing from
     * the POINT size on a 4K panel gives a 1920-wide picture drawn into a 3840-wide target:
     * the destination rectangle is half the drawable and every game pixel becomes a 2x2 block.
     * Nothing in the port does this now -- the check is that the two are genuinely different
     * numbers, so the assertions above are not both trivially satisfied. */
    {
        float px, py, pwid, phgt, xx, xy, xw, xh;
        geom_compose_rect(1920, 1080, geom_compose_width(1920, 1080, 4096), GEOM_SCREEN_H,
                          &px, &py, &pwid, &phgt);
        geom_compose_rect(3840, 2160, geom_compose_width(3840, 2160, 4096), GEOM_SCREEN_H,
                          &xx, &xy, &xw, &xh);
        /* The two fill their own drawable -- that is test_scale's property, restated here only
         * so the ratio below is not being taken between two numbers that could both be wrong. */
        eqf1("composed from 1920 the picture is 1920 wide", pwid, 1920.0f);
        eqf1("composed from 3840 the picture is 3840 wide", xw,   3840.0f);
        /* THE BUG: composing from the point size on a 4K panel would draw the FIRST of these
         * into a target the size of the SECOND, so every game pixel becomes a 2x2 block. The
         * numbers have to differ by exactly the density for that to be the failure it is. */
        eqf1("and the two differ by the density, which is what an upscale would have hidden",
             xw / pwid * 1000.0f, 2000.0f);
        eq("the field of view is identical either way",
           geom_compose_width(3840, 2160, 4096), geom_compose_width(1920, 1080, 4096));
    }

    /* A density of zero or a negative one is a broken SDL answer, not a reason to divide by
     * it: it reads as unscaled. Asserted because the alternative is a pointer at infinity and
     * a silent one. */
    {
        float bx = 0, by = 0, rx = 0, ry = 0;
        geom_pointer_to_compose(1920, 1080, 978, GEOM_SCREEN_H, 0.0f, 400.0f, 300.0f, &bx, &by);
        geom_pointer_to_compose(1920, 1080, 978, GEOM_SCREEN_H, 1.0f, 400.0f, 300.0f, &rx, &ry);
        eqf("a density of 0 reads as unscaled, in x", bx, rx);
        eqf("a density of 0 reads as unscaled, in y", by, ry);
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
     * tools/routes/background_test.sh's byte-identity arm true. */
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

/* The stage-mode walk bound (issue #43). The section's two words say one thing between them:
 * when the camera is at its bound, the walkable area ends at the screen's RIGHT EDGE. */
static void walk_bound(void)
{
    /* THE GAME'S OWN WIDTH IS UNTOUCHED, by construction and not by arithmetic that agrees.
     * This matters because the game has the same situation at 4:3 whenever a section's B is
     * under 794, and matching it there is not optional. */
    eq("at 794 the game's own B stands", geom_walk_max(900, 106, 794), 900);
    eq("even when B is under the screen and the game "
                                              "itself shows stage past it", geom_walk_max(500, 0, 794), 500);
    eq("and at the end of a stage", geom_walk_max(3200, 2406, 794), 3200);

    /* THE REPORTED CASE, with the numbers off the run that produced it: stage 1-1's first
     * section locks the camera at 106, so B = 900; the view is 978; B - view is negative so
     * the camera clamps at 0 and the screen's right edge is 978, not 900. */
    eq("a clamped camera widens the walk bound to the "
                                              "screen's right edge", geom_walk_max(900, 0, 978), 978);

    /* WHERE THE CAMERA CAN STILL REACH ITS BOUND, nothing changes -- the edge already IS B.
     * A build that widened unconditionally would move this one and no other. */
    eq("a camera that reached its bound needs no "
                                                   "widening: 1022 + 978 is exactly B", geom_walk_max(2000, 1022, 978), 2000);
    eq("and past it, the edge wins", geom_walk_max(2000, 1100, 978), 2078);

    /* IT NEVER SHRINKS. The bound is the stage's own boundary and widening is the only
     * direction this is allowed to move it. */
    for (int view = 794; view <= 3840; view += 7)
        for (int cam = 0; cam <= 2000; cam += 311)
            eq("the walk bound is never pulled in below the game's B", (geom_walk_max(900, cam, view) >= 900) ? 1 : 0, 1);

    /* AND IT IS EXACTLY THE EDGE WHENEVER IT MOVES, so this cannot drift into a fudge. */
    for (int view = 795; view <= 3840; view += 13)
        for (int cam = 0; cam <= 2000; cam += 197) {
            const int w = geom_walk_max(900, cam, view);
            eq("a widened bound is the screen's right edge and nothing else", (w == 900 || w == cam + view) ? 1 : 0, 1);
        }
}

int main(void)
{
    walk_bound();
    test_scale();
    test_screen_align();
    test_unproject();
    test_density();
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
