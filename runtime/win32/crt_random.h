#ifndef LF2_WIN32_CRT_RANDOM_H
#define LF2_WIN32_CRT_RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MSVCR80's rand/srand: one generator per guest thread, and LF2 has one guest thread. Its state
 * is guest-visible session state, so a snapshot saves it and two peers agree on it. */
typedef struct {
    uint32_t holdrand;
} CrtRandom;

void crt_random_seed(CrtRandom *random, uint32_t seed);
uint32_t crt_random_next(CrtRandom *random);
CrtRandom *crt_random_guest(void);

#ifdef __cplusplus
}
#endif

#endif
