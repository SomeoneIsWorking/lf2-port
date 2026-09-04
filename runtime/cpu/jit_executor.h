#ifndef LF2_JIT_EXECUTOR_H
#define LF2_JIT_EXECUTOR_H

#include <stdint.h>

/* The single LF2-to-x86port execution boundary. The shared runtime JIT is the
 * default; any bounded fallback stays inside x86port, is reason-coded and
 * counted, and is never exposed as an LF2 interpreter selector. */
void lf2_jit_call(uint32_t guest_address);
void lf2_jit_call_original(uint32_t guest_address);
void lf2_jit_invalidate(uint32_t guest_address, uint32_t length);

#endif
