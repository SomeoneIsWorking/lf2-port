/* The object-sprite sampling chain (issue #112), walked offline.
 *
 * quad.frag evaluates a chain; runtime/video/spritefilter.h decides what a chain may be, when
 * a quad carries one, what an AUTO factor resolves to and how far an outline grows the
 * geometry. Those are exactly the claims that cost a three-minute GPU run to check, so they
 * are asserted here in a millisecond -- and the overrides include the same header, so this is
 * not walking a copy.
 *
 * It also mirrors the shader's COORDINATE WALK (nearest_run + the top-level sample) on the
 * CPU and pins three properties of it that a picture cannot show convincingly:
 *
 *   - `nearest:N` alone must be EXACTLY plain nearest: integer magnification followed by an
 *     integer-grid sample changes no pixel. This is what makes the GPU arm honest -- if the
 *     chain drifts, this equality is the first thing that breaks.
 *   - `nearest:1/2` must show one texel of every two, twice each -- the coarsening the player
 *     asked for, not a blur and not a shift.
 *   - a chain's passes must COMPOSE: appending `nearest:2` after `nearest:1/2` must not undo
 *     the halving, which is the whole point of quantising inside every pass.
 */
#include "spritefilter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;

static void ok(const char *what, int cond)
{
    checks++;
    if (cond) return;
    failures++;
    printf("FAIL %s\n", what);
}

static int near_eq(float a, float b) { return fabsf(a - b) < 1e-4f; }

/* quad.frag's nearest_run() plus its floor() at the art: which SOURCE TEXEL a fragment at
 * position `p` source texels ends up sampling, under a chain of nearest passes. */
static int chain_pick(const SpriteChain *c, float p, float auto_factor)
{
    float x = p * spritechain_total_factor(c, auto_factor);
    for (int i = c->count - 1; i >= 0; i--) x = (floorf(x) + 0.5f) / spritechain_pass_factor(&c->pass[i], auto_factor);
    return (int)floorf(x);
}

static SpriteChain parsed(const char *spec)
{
    SpriteChain c;
    char err[128];
    if (!spritechain_parse(spec, &c, err, sizeof err)) {
        printf("FAIL parse of '%s': %s\n", spec, err);
        failures++;
        spritechain_clear(&c);
    }
    checks++;
    return c;
}

static int refuses(const char *spec)
{
    SpriteChain c;
    char err[128];
    err[0] = '\0';
    if (spritechain_parse(spec, &c, err, sizeof err)) return 0;
    return err[0] != '\0'; /* a refusal that says nothing is not a refusal */
}

int main(void)
{
    /* ---- what parses, and what is refused BY NAME ---- */
    {
        const SpriteChain c = parsed("nearest:1/2,nearest:2,aa,inner,outline:1");
        ok("chain keeps its passes in order", c.count == 2 && c.pass[0].den == 2 && c.pass[1].num == 2);
        ok("chain reads the terminal steps", c.smooth == 1 && c.inner == 1 && c.outline == 1);
        char buf[128] = "";
        spritechain_format(&c, buf, sizeof buf);
        ok("chain round-trips through the config string", strcmp(buf, "nearest:1/2,nearest:2,aa,inner,outline:1") == 0);
    }
    {
        const SpriteChain c = parsed("linear:auto,aa");
        ok("AUTO parses as a factor", c.count == 1 && c.pass[0].num == 0 && c.pass[0].kind == SPRITE_LINEAR);
        char buf[64] = "";
        spritechain_format(&c, buf, sizeof buf);
        ok("AUTO round-trips", strcmp(buf, "linear:auto,aa") == 0);
    }
    {
        SpriteChain c;
        ok("an absent spec is the empty chain", spritechain_parse(NULL, &c, NULL, 0) && !spritechain_active(&c));
        ok("an empty spec is the empty chain", spritechain_parse("", &c, NULL, 0) && !spritechain_active(&c));
    }
    ok("an unknown pass is refused", refuses("hqx:2"));
    ok("a pass with no factor is refused", refuses("nearest"));
    ok("a 1:1 pass is refused", refuses("nearest:1"));
    ok("a factor past the cap is refused", refuses("nearest:16"));
    ok("a denominator past the cap is refused", refuses("nearest:1/16"));
    ok("junk after a factor is refused", refuses("nearest:2x"));
    {
        /* Built from the cap rather than spelled out, so raising the cap does not quietly
         * turn this check into an assertion about a chain that now fits. */
        char spec[512] = "";
        size_t used = 0;
        for (int i = 0; i <= SPRITE_PASS_MAX; i++)
            used += (size_t)snprintf(spec + used, sizeof spec - used, "%snearest:2", used ? "," : "");
        ok("more passes than the cap is refused", refuses(spec));
        char fits[512] = "";
        used = 0;
        for (int i = 0; i < SPRITE_PASS_MAX; i++)
            used += (size_t)snprintf(fits + used, sizeof fits - used, "%snearest:2", used ? "," : "");
        ok("exactly the cap is allowed", !refuses(fits));
    }
    ok("a second linear pass is refused", refuses("linear:2,linear:1/2"));
    ok("inner refuses an argument", refuses("inner:2"));
    ok("an outline past the cap is refused", refuses("outline:9"));
    ok("an empty outline thickness is refused", refuses("outline:"));
    ok("junk after an outline thickness is refused", refuses("outline:1junk"));
    ok("outline zero is refused instead of silently disabling itself", refuses("outline:0"));
    ok("one linear pass is allowed", !refuses("linear:2,nearest:1/2"));

    /* ---- when a quad carries the chain ---- */
    {
        SpriteChain empty;
        spritechain_clear(&empty);
        const SpriteChain c = parsed("nearest:2");
        ok("an empty chain never splits batches", !spritechain_needs_own_draw(&empty, 1, 0));
        ok("only objects carry a chain", !spritechain_needs_own_draw(&c, 0, 0));
        ok("host tiles never carry a chain", !spritechain_needs_own_draw(&c, 1, 1));
        ok("a guest object sprite draws alone", spritechain_needs_own_draw(&c, 1, 0));
        const SpriteChain aa_only = parsed("aa");
        ok("a terminal step alone is enough to split", spritechain_needs_own_draw(&aa_only, 1, 0));
        const SpriteChain outline_only = parsed("outline:1");
        ok("an outline alone is enough to split", spritechain_needs_own_draw(&outline_only, 1, 0));
        const SpriteChain inner_only = parsed("inner");
        ok("an inner contour alone is enough to split", spritechain_needs_own_draw(&inner_only, 1, 0));
    }

    /* ---- the AUTO factor ---- */
    ok("auto rounds 2x up to 2", near_eq(spritechain_auto_factor(40, 40, 80, 80), 2.0f));
    ok("auto gives 2 at the view's real scale", near_eq(spritechain_auto_factor(100, 100, 196, 196), 2.0f));
    ok("auto gives 3 past the midpoint", near_eq(spritechain_auto_factor(100, 100, 260, 260), 3.0f));
    ok("auto floors at 1", near_eq(spritechain_auto_factor(794, 550, 794, 550), 1.0f));
    ok("empty span falls back to 1", near_eq(spritechain_auto_factor(0, 10, 80, 80), 1.0f));

    /* ---- the outline's margin, in source texels ---- */
    {
        const SpriteChain none = parsed("nearest:2");
        ok("no outline moves no geometry", near_eq(spritechain_margin_texels(&none, 2.0f), 0.0f));
        const SpriteChain inner = parsed("inner");
        ok("an inner contour does not grow the geometry", near_eq(spritechain_margin_texels(&inner, 2.0f), 0.0f));
        const SpriteChain up = parsed("nearest:2,outline:1");
        ok("a chain pixel is 1/F source texels", near_eq(spritechain_margin_texels(&up, 2.0f), 0.5f));
        const SpriteChain down = parsed("nearest:1/2,outline:2");
        ok("a coarsened chain needs a wider margin", near_eq(spritechain_margin_texels(&down, 2.0f), 4.0f));
        const SpriteChain autoup = parsed("nearest:auto,outline:1");
        ok("AUTO reaches the margin too", near_eq(spritechain_margin_texels(&autoup, 4.0f), 0.25f));
    }

    /* ---- the LINEAR cut the shader is built around ---- */
    {
        const SpriteChain none = parsed("nearest:2,nearest:1/2");
        ok("a chain with no linear pass cuts past its end", spritechain_linear_cut(&none) == none.count);
        const SpriteChain one = parsed("nearest:2,linear:1/2");
        ok("the cut is the linear pass's index", spritechain_linear_cut(&one) == 1);
    }

    /* ---- the coordinate walk ---- */
    {
        const SpriteChain up = parsed("nearest:2");
        int exact = 1;
        for (int k = 0; k < 4000; k++) {
            const float p = (float)k * 0.0625f + 0.03125f;
            if (chain_pick(&up, p, 1.0f) != (int)floorf(p)) exact = 0;
        }
        ok("nearest:2 is EXACTLY plain nearest", exact);

        const SpriteChain half = parsed("nearest:1/2");
        int coarse = 1;
        for (int texel = 0; texel < 64; texel++) {
            const int want = 2 * (texel / 2) + 1;
            /* both halves of a source-texel pair must resolve to the same odd texel */
            if (chain_pick(&half, (float)texel + 0.25f, 1.0f) != want) coarse = 0;
            if (chain_pick(&half, (float)texel + 0.75f, 1.0f) != want) coarse = 0;
        }
        ok("nearest:1/2 shows one texel of every two, twice", coarse);

        const SpriteChain half_then_double = parsed("nearest:1/2,nearest:2");
        int composed = 1;
        for (int k = 0; k < 512; k++) {
            const float p = (float)k * 0.125f + 0.0625f;
            if (chain_pick(&half_then_double, p, 1.0f) != chain_pick(&half, p, 1.0f)) composed = 0;
        }
        ok("magnifying after halving does not undo the halving", composed);
    }

    /* ---- the edge wedge `aa` reconstructs (issue #113) ---- */
    {
        ok("the 45-degree cut line is half covered",
           near_eq(spritechain_edge_coverage(0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f), 0.5f));
        ok("the far side of the pixel is untouched",
           near_eq(spritechain_edge_coverage(0.9f, 0.9f, 0.5f, 0.5f, 0.25f, 0.25f), 0.0f));
        ok("the corner itself is fully covered",
           near_eq(spritechain_edge_coverage(0.0f, 0.0f, 0.5f, 0.5f, 0.25f, 0.25f), 1.0f));
        ok("a shallow continuation reaches farther along u",
           spritechain_edge_coverage(0.6f, 0.1f, 0.9f, 0.5f, 0.25f, 0.25f) >
               spritechain_edge_coverage(0.6f, 0.1f, 0.5f, 0.5f, 0.25f, 0.25f));
        ok("a steep continuation reaches farther along v",
           spritechain_edge_coverage(0.1f, 0.6f, 0.5f, 0.9f, 0.25f, 0.25f) >
               spritechain_edge_coverage(0.1f, 0.6f, 0.5f, 0.5f, 0.25f, 0.25f));

        const float corner = spritechain_edge_coverage(0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f);
        ok("at 2x the corner fragment is genuinely blended", corner > 0.2f && corner < 0.8f);
        ok("at 2x the fragment across the cut is not",
           near_eq(spritechain_edge_coverage(0.75f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f));

        /* And at 4x, where a box filter degenerates just as badly, the cut still lands on real
         * fragments. Measured: 2 of the 16 fragments in the pixel fall strictly inside the
         * ramp band, the rest landing on its ends at exactly 0 or 1 -- which is what a one-
         * pixel-wide line across a 4x4 grid of sample points looks like. */
        int blended = 0;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                const float w = spritechain_edge_coverage(((float)x + 0.5f) / 4.0f, ((float)y + 0.5f) / 4.0f, 0.5f,
                                                          0.5f, 0.25f, 0.25f);
                if (w > 0.0f && w < 1.0f) blended++;
            }
        ok("at 4x the reconstructed line still crosses fragments", blended >= 2);

        /* Monotone, bounded, and no NaN when a degenerate quad gives a zero footprint. */
        int monotone = 1;
        for (int r = 1; r <= 16; r++) {
            float prev = 2.0f;
            for (int k = 0; k <= 400; k++) {
                const float w =
                    spritechain_edge_coverage((float)k / 400.0f, 0.1f, 0.5f, 0.5f, (float)r / 16.0f, (float)r / 16.0f);
                if (w > prev || w < 0.0f || w > 1.0f) monotone = 0;
                prev = w;
            }
        }
        ok("coverage falls monotonically away from the corner", monotone);
        ok("a zero footprint is a hard cut, not a NaN",
           near_eq(spritechain_edge_coverage(0.6f, 0.1f, 0.5f, 0.5f, 0.0f, 0.0f), 0.0f));
    }

    if (failures) {
        printf("%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("spritefilter: %d checks passed\n", checks);
    return 0;
}
