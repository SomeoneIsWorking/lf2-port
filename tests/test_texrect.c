#include <math.h>
#include <stdio.h>

#include "texrect.h"

static int checks, fails;

static void near(const char *what, float got, float want)
{
    checks++;
    if (fabsf(got - want) > 0.000001f) {
        fprintf(stderr, "FAIL %s: got %.9f, want %.9f\n", what, got, want);
        fails++;
    }
}

static void stretch_axis_matches_software(int source, int destination)
{
    float u0, v0, u1, v1;
    texrect_for_blit(17, 9, (float)source, 7, destination, 7, 1024, 1024, 0, &u0, &v0, &u1, &v1);
    for (int x = 0; x < destination; x++) {
        const float t = ((float)x + 0.5f) / (float)destination;
        const int sampled = (int)((u0 + t * (u1 - u0)) * 1024.0f);
        const int software = 17 + x * source / destination;
        checks++;
        if (sampled != software) {
            fprintf(stderr, "FAIL stretch %d -> %d at %d: sampled %d, want %d\n", source, destination, x, sampled,
                    software);
            fails++;
        }
    }
}

static void whole_surface_stretch_matches_software(void)
{
    float u0, v0, u1, v1;
    texrect_for_blit(0, 0, 800, 104, 1142, 104, 800, 104, 0, &u0, &v0, &u1, &v1);
    for (int x = 0; x < 1142; x++) {
        const float t = ((float)x + 0.5f) / 1142.0f;
        const int sampled = (int)((u0 + t * (u1 - u0)) * 800.0f);
        const int software = x * 800 / 1142;
        checks++;
        if (sampled != software) {
            fprintf(stderr, "FAIL whole-surface stretch at %d: sampled %d, want %d\n", x, sampled, software);
            fails++;
        }
    }
}

static int magnified_stretch_matches_output(float output_x, float output_w)
{
    enum { SOURCE_X = 17, SOURCE_W = 41, LOGICAL_W = 17, SHEET_W = 1024 };
    const int first = (int)ceilf(output_x - 0.5f);
    const int end = (int)ceilf(output_x + output_w - 0.5f);
    const int raster_w = end - first;
    float u0, v0, u1, v1;
    texrect_for_output_blit(SOURCE_X, 9, SOURCE_W, 7, LOGICAL_W, 7, output_x, 0.63f, output_w, 21.2f, SHEET_W, 1024, 0,
                            &u0, &v0, &u1, &v1);

    /* X is a true logical StretchBlt and therefore maps once across the full output raster.
     * Y is a 1:1 logical copy and must retain its texel-centre interval despite magnification. */
    near("magnified stretch keeps 1:1 axis top", v0, 9.5f / 1024.0f);
    near("magnified stretch keeps 1:1 axis bottom", v1, 15.5f / 1024.0f);
    for (int pixel = first; pixel < end; pixel++) {
        const float t = ((float)pixel + 0.5f - output_x) / output_w;
        const int sampled = (int)((u0 + t * (u1 - u0)) * (float)SHEET_W);
        const int expected = SOURCE_X + (pixel - first) * SOURCE_W / raster_w;
        checks++;
        if (sampled != expected) {
            fprintf(stderr, "FAIL magnified stretch at output %d: sampled %d, want %d\n", pixel, sampled, expected);
            fails++;
        }
    }

    /* Negative: blanket logical dimensions run the 41->17 rule continuously over the output
     * quad instead of selecting the full-resolution one-stage stretch contract above. */
    texrect_for_blit(SOURCE_X, 9, SOURCE_W, 7, LOGICAL_W, 7, SHEET_W, 1024, 0, &u0, &v0, &u1, &v1);
    int mismatches = 0;
    for (int pixel = first; pixel < end; pixel++) {
        const float t = ((float)pixel + 0.5f - output_x) / output_w;
        const int sampled = (int)((u0 + t * (u1 - u0)) * (float)SHEET_W);
        const int expected = SOURCE_X + (pixel - first) * SOURCE_W / raster_w;
        mismatches += sampled != expected;
    }
    return mismatches;
}

static void overlay_output_sample_uses_logical_destination(void)
{
    enum {
        SHEET_W = 794,
        SHEET_H = 600,
        SOURCE_X = 330,
        SOURCE_Y = 254,
        SOURCE_W = 279,
        SOURCE_H = 22,
        COMPOSITION_W = 1070,
        COMPOSITION_H = 550,
        OUTPUT_W = 3840,
        OUTPUT_H = 1975,
    };
    const float scale = (float)OUTPUT_H / (float)COMPOSITION_H;
    const float composition_x = ((float)COMPOSITION_W - (float)SHEET_W) * 0.5f;
    const float output_x = ((float)OUTPUT_W - (float)COMPOSITION_W * scale) * 0.5f;
    const float dst_x = (composition_x + 15.0f) * scale + output_x;
    const float dst_y = 87.0f * scale;
    const float dst_w = (float)SOURCE_W * scale;
    const float dst_h = (float)SOURCE_H * scale;
    const float first_x = floorf(dst_x) + 0.5f;
    const float first_y = floorf(dst_y) + 0.5f;
    const float tx = (first_x - dst_x) / dst_w;
    const float ty = (first_y - dst_y) / dst_h;
    float u0, v0, u1, v1;

    /* A logical 1:1 sprite copy keeps its source texel centres under output magnification, so
     * this first covered output fragment still belongs to source (330,254). */
    texrect_for_output_blit(SOURCE_X, SOURCE_Y, SOURCE_W, SOURCE_H, SOURCE_W, SOURCE_H, dst_x, dst_y, dst_w, dst_h,
                            SHEET_W, SHEET_H, 0, &u0, &v0, &u1, &v1);
    const int logical_x = (int)((u0 + tx * (u1 - u0)) * (float)SHEET_W);
    const int logical_y = (int)((v0 + ty * (v1 - v0)) * (float)SHEET_H);
    checks += 2;
    if (logical_x != SOURCE_X || logical_y != SOURCE_Y) {
        fprintf(stderr, "FAIL logical overlay mapping sampled (%d,%d), want (%d,%d)\n", logical_x, logical_y, SOURCE_X,
                SOURCE_Y);
        fails++;
    }

    /* Falsifier for issue #96's old draw_texture_quad call: using the already-scaled raster
     * extent re-applies stretch mapping and moves the same fragment into the green separator. */
    texrect_for_blit(SOURCE_X, SOURCE_Y, SOURCE_W, SOURCE_H, (int)dst_w, (int)dst_h, SHEET_W, SHEET_H, 0, &u0, &v0, &u1,
                     &v1);
    const int raster_x = (int)((u0 + tx * (u1 - u0)) * (float)SHEET_W);
    const int raster_y = (int)((v0 + ty * (v1 - v0)) * (float)SHEET_H);
    checks += 2;
    if (raster_x != SOURCE_X - 1 || raster_y != SOURCE_Y - 1) {
        fprintf(stderr, "FAIL raster-feedback negative sampled (%d,%d), want adjacent (%d,%d)\n", raster_x, raster_y,
                SOURCE_X - 1, SOURCE_Y - 1);
        fails++;
    }
}

int main(void)
{
    float u0, v0, u1, v1;
    texrect_centres(84, 32, 27, 28, 300, 300, &u0, &v0, &u1, &v1);
    near("left is first texel centre", u0, 84.5f / 300.0f);
    near("top is first texel centre", v0, 32.5f / 300.0f);
    near("right is last texel centre", u1, 110.5f / 300.0f);
    near("bottom is last texel centre", v1, 59.5f / 300.0f);

    /* The discriminator: the old boundary coordinates must not satisfy this test. */
    checks++;
    if (fabsf(u0 - 84.0f / 300.0f) < 0.000001f) {
        fprintf(stderr, "FAIL left coordinate still addresses the shared cell boundary\n");
        fails++;
    }

    overlay_output_sample_uses_logical_destination();

    int mismatches = magnified_stretch_matches_output(0.0f, 51.0f);
    checks++;
    if (mismatches != 41) {
        fprintf(stderr, "FAIL magnified 41->17 negative disagreed on %d fragments, want 41\n", mismatches);
        fails++;
    }
    mismatches = magnified_stretch_matches_output(0.37f, 51.4f);
    checks++;
    if (mismatches != 43) {
        fprintf(stderr, "FAIL fractional-position negative disagreed on %d fragments, want 43\n", mismatches);
        fails++;
    }

    /* The two Lion Forest backdrop widths that exposed the GPU/software divergence, plus
     * reduction and an awkward ratio whose integer boundaries repeat. */
    stretch_axis_matches_software(800, 1142);
    stretch_axis_matches_software(300, 429);
    stretch_axis_matches_software(41, 17);
    stretch_axis_matches_software(35, 64);
    whole_surface_stretch_matches_software();

    if (fails) return 1;
    printf("texrect: %d checks passed\n", checks);
    return 0;
}
