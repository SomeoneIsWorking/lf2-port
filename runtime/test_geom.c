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

/* ---- the composition follows the window's PIXEL width ---- */
static void test_compose(void)
{
    enum { WIDE_MAX = 4096 };
    eq("compose 794 -> 794",        geom_compose_width(794, WIDE_MAX), 794);
    eq("compose 1600 -> 1600",      geom_compose_width(1600, WIDE_MAX), 1600);
    /* The case that flipped when the aspect term came out: it used to be 550*1920/1080 = 978. */
    eq("compose 1920 -> 1920",      geom_compose_width(1920, WIDE_MAX), 1920);
    /* And the one that shows the height no longer enters the formula at all. */
    eq("compose 800 -> 800",        geom_compose_width(800, WIDE_MAX), 800);
    eq("compose 400 clamps up",     geom_compose_width(400, WIDE_MAX), 794);
    eq("compose 9000 clamps down",  geom_compose_width(9000, WIDE_MAX), WIDE_MAX);
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

/* ---- object magnification ---- */
static void test_object_scale(void)
{
    eq("the game's own window is 1x", geom_object_scale(550), 1);
    eq("1080 rows is 2x",             geom_object_scale(1080), 2);
    eq("1600 rows is 3x",             geom_object_scale(1600), 3);
    eq("a tiny window never goes below 1x", geom_object_scale(100), 1);
    /* Rounded, not truncated: 800/550 is 1.45, which is nearer 1 than 2. */
    eq("800 rounds to 1x",  geom_object_scale(800), 1);
    eq("850 rounds to 2x",  geom_object_scale(850), 2);
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
    test_compose();
    test_parallax();
    test_camera();
    test_draw_camera();
    test_object_scale();
    test_overlay_rows();
    test_pan();

    printf("geometry: %d checks, %d failure(s)\n", checks, failures);
    if (!checks) {
        printf("  FAIL  no checks ran at all, so this says NOTHING\n");
        return 1;
    }
    return failures ? 1 : 0;
}
