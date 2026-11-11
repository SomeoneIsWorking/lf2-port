#include "crt_random.h"

#include <stdio.h>

/* MSVC's documented sequences: unseeded (state 1) and srand(0). */
static const uint32_t UNSEEDED[] = {41, 18467, 6334, 26500, 19169};
static const uint32_t SEED_ZERO[] = {38, 7719, 21238, 2437, 8855};

static int check(const char *name, CrtRandom *random, const uint32_t *expected, int count)
{
    for (int i = 0; i < count; i++) {
        const uint32_t got = crt_random_next(random);
        if (got != expected[i]) {
            fprintf(stderr, "%s[%d]: got %u, expected %u\n", name, i, got, expected[i]);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    CrtRandom random = *crt_random_guest();
    int failures = check("unseeded", &random, UNSEEDED, 5);
    crt_random_seed(&random, 0);
    failures += check("srand(0)", &random, SEED_ZERO, 5);
    crt_random_seed(&random, 1);
    failures += check("srand(1)", &random, UNSEEDED, 5);
    return failures != 0;
}
