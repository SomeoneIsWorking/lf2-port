#include "crt_random.h"

static CrtRandom guest_random = {1u}; /* the CRT's state before any srand */

void crt_random_seed(CrtRandom *random, uint32_t seed)
{
    random->holdrand = seed;
}

uint32_t crt_random_next(CrtRandom *random)
{
    random->holdrand = random->holdrand * 214013u + 2531011u;
    return (random->holdrand >> 16) & 0x7fffu;
}

CrtRandom *crt_random_guest(void)
{
    return &guest_random;
}
