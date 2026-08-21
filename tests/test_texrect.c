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
