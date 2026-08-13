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

int main(void)
{
    float u0, v0, u1, v1;
    texrect_centres(84, 32, 27, 28, 300, 300, &u0, &v0, &u1, &v1);
    near("left is first texel centre",   u0, 84.5f / 300.0f);
    near("top is first texel centre",    v0, 32.5f / 300.0f);
    near("right is last texel centre",   u1, 110.5f / 300.0f);
    near("bottom is last texel centre",  v1, 59.5f / 300.0f);

    /* The discriminator: the old boundary coordinates must not satisfy this test. */
    checks++;
    if (fabsf(u0 - 84.0f / 300.0f) < 0.000001f) {
        fprintf(stderr, "FAIL left coordinate still addresses the shared cell boundary\n");
        fails++;
    }

    if (fails) return 1;
    printf("texrect: %d checks passed\n", checks);
    return 0;
}
