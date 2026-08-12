/* The hand-woven stage geometry loader, tested without booting the game (issue #62).
 *
 * The loader takes no game data -- the caller resolves a layer name to a span and a stage width
 * -- which is exactly what makes this offline, and it is why the dependency is a function
 * pointer rather than a call into the guest.
 *
 * WHAT THIS IS REALLY FOR. Every failure mode of a data loader is silent: a file that does not
 * exist reads as "this stage has no geometry", a key it does not understand reads as a solid
 * that never appears, a layer name it cannot resolve reads as a solid at the wrong depth, and
 * an OBJ line it skips reads as a model with a hole in it. None of those crash and none of them
 * look like a bug in a screenshot. So most of what is asserted here is that the loader REFUSES,
 * and says which line.
 */
#include "video/stagegeom.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void ok(const char *what, int cond)
{
    checks++;
    if (cond) return;
    failures++;
    printf("  FAIL  %s\n", what);
}

static void eqf(const char *what, float got, float want)
{
    checks++;
    const float d = got - want;
    if (d > -0.01f && d < 0.01f) return;
    failures++;
    printf("  FAIL  %s: got %.4f, expected %.4f\n", what, (double)got, (double)want);
}

/* The Great Wall's real numbers: hill1 has span 1204 on a 2400-wide stage, so its depth is
 * 1606/410 = 3.917 (claim C031).
 *
 * `flat.bmp` at span 794 is the case with NO derivable depth -- a layer whose span is the
 * game's own screen width has a scroll range of zero, so it never moves and is infinitely far.
 * The first cut of this test used The Great Wall's real `sky` (span 800) for it and the check
 * failed, correctly: 800 gives a six-pixel scroll range and a depth of 267, which is very far
 * away but perfectly derivable. The fixture was wrong, not the loader. */
static int layers(void *ctx, const char *name, int *span, int *stage_width)
{
    (void)ctx;
    *stage_width = 2400;
    if (strcmp(name, "hill1.bmp") == 0) { *span = 1204; return 1; }
    if (strcmp(name, "sky.bmp")   == 0) { *span = 800;  return 1; }
    if (strcmp(name, "flat.bmp")  == 0) { *span = 794;  return 1; }
    return 0;
}

static const char *DIR = "fixtures/stages";

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : DIR;
    StageGeom g;

    /* ---- ABSENCE IS NOT FAILURE, which is the state of every stage until one is authored ---- */
    {
        const int r = stagegeom_load(dir, "No_Such_Stage_At_All", layers, NULL, &g);
        ok("a stage with no .stage file loads successfully", r == 1);
        ok("...with no vertices", g.n == 0);
        ok("...and no error", g.error[0] == 0);
        stagegeom_free(&g);
    }

    /* ---- the fixture, which is two solids of one card each ---- */
    {
        const int r = stagegeom_load(dir, "Great_Wall_Fixture", layers, NULL, &g);
        if (!r) {
            printf("  FAIL  the fixture did not load: %s\n", g.error);
            failures++;
            return 1;
        }
        ok("two solids loaded", g.solids == 2);
        /* Two triangles per card, two cards: 12 vertices. */
        ok("12 vertices, so both faces of both cards arrived", g.n == 12);

        /* THE SKIPPED-LINE COUNT, which is the whole reason it exists. The fixture contains
         * mtllib, o, usemtl and s -- four lines this loader does not read -- twice, once per
         * solid, because the same model is loaded twice. A loader that silently ignored them
         * would report 0 here and look identical. */
        ok("the OBJ lines this loader does not read were COUNTED, not hidden",
           g.skipped_lines == 8);

        /* THE DEPTH CAME FROM THE GAME'S OWN LAYER. 1606/410. */
        eqf("solid 1 took hill1.bmp's derived depth", g.v[0].depth, 1606.0f / 410.0f);
        /* ...and the second solid's literal depth is the fighters' plane. */
        eqf("solid 2 took its literal depth", g.v[6].depth, 1.0f);

        /* `at:` is added to every vertex, and the OBJ's y and z become jump and row -- which is
         * the whole reason a three-axis model file is enough for a four-axis engine. */
        eqf("the first vertex's x is the model's 0 plus at's 1200", g.v[0].x, 1200.0f);
        eqf("its jump is the model's y", g.v[0].jump, 0.0f);
        eqf("its row is the model's z plus at's 400", g.v[0].row, 400.0f);
        eqf("the second vertex's x is the model's 10 plus 1200", g.v[1].x, 1210.0f);
        eqf("the third vertex's jump is the model's 20", g.v[2].jump, 20.0f);

        /* The tint is 0..255 in the file and 0..1 in the vertex. */
        eqf("the tint's red", g.v[0].r, 1.0f);
        eqf("the tint's green", g.v[0].g, 128.0f / 255.0f);
        eqf("the tint's blue", g.v[0].b, 0.0f);
        eqf("a solid with no tint is white", g.v[6].r, 1.0f);
        eqf("...in all three channels", g.v[6].g, 1.0f);

        /* UVs and normals come through from the OBJ. */
        eqf("the first vertex's u", g.v[0].u, 0.0f);
        eqf("the second vertex's u", g.v[1].u, 1.0f);
        eqf("the normal is the OBJ's", g.v[0].nz, 1.0f);
        stagegeom_free(&g);
    }

    /* ---- AND EVERY WAY IT MUST REFUSE. These are the assertions that matter: each of these
     *      failures is silent, and a loader that accepted them would produce a stage that is
     *      subtly wrong rather than one that does not load. ---- */
    {
        const struct { const char *name, *must_mention; } bad[] = {
            { "Bad_Unknown_Key",   "not a key this format has" },
            { "Bad_No_Depth",      "needs both a model and a depth" },
            { "Bad_Missing_Layer", "has no layer named" },
            { "Bad_Flat_Layer",    "NO derivable depth" },
            { "Bad_Unclosed",      "ends inside a solid" },
            { "Bad_Outside",       "outside any solid" },
            { "Bad_No_Model_File", "cannot be opened" },
        };
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            const int r = stagegeom_load(dir, bad[i].name, layers, NULL, &g);
            char what[192];
            snprintf(what, sizeof what, "%s is REFUSED", bad[i].name);
            ok(what, r == 0);
            snprintf(what, sizeof what, "%s says why (mentions \"%s\")",
                     bad[i].name, bad[i].must_mention);
            ok(what, strstr(g.error, bad[i].must_mention) != NULL);
            if (r == 0 && !strstr(g.error, bad[i].must_mention))
                printf("        it said: %s\n", g.error);
            stagegeom_free(&g);
        }
    }

    printf("stage geometry: %d checks, %d failure(s)\n", checks, failures);
    if (!checks) {
        printf("  FAIL  no checks ran at all, so this says NOTHING\n");
        return 1;
    }
    return failures ? 1 : 0;
}
