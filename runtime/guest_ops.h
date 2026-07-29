/* Helpers the generated code calls. Kept out of guest.h so the generated file has one
 * obvious place to look for anything it references. */
#ifndef GUEST_OPS_H
#define GUEST_OPS_H

#include "guest.h"

#include <stdio.h>

/* Byte registers: 0-3 are AL..BL (low byte), 4-7 are AH..BH (bits 8-15 of EAX..EBX). */
static inline uint8_t GETR8(unsigned i)
{
    return (i < 4) ? (uint8_t)R(i) : (uint8_t)(R(i - 4) >> 8);
}

static inline void SETR8(unsigned i, uint32_t v)
{
    if (i < 4) R(i) = (R(i) & ~0xffu) | (v & 0xffu);
    else       R(i - 4) = (R(i - 4) & ~0xff00u) | ((v & 0xffu) << 8);
}

#define DEF_SHIFT(w, type)                                                              \
    static inline uint32_t op_shl##w(uint32_t v, unsigned n) {                          \
        uint32_t r = (uint32_t)((type)v << n);                                          \
        FLAGS(F_SHL, (w) / 8, v, n, r); return r; }                                     \
    static inline uint32_t op_shr##w(uint32_t v, unsigned n) {                          \
        uint32_t r = (uint32_t)((type)v >> n);                                          \
        FLAGS(F_SHR, (w) / 8, v, n, r); return r; }                                     \
    static inline uint32_t op_sar##w(uint32_t v, unsigned n) {                          \
        uint32_t r = (uint32_t)(int##w##_t)((int##w##_t)(type)v >> n);                  \
        FLAGS(F_SAR, (w) / 8, v, n, r); return r; }                                     \
    static inline uint32_t op_rol##w(uint32_t v, unsigned n) {                          \
        type x = (type)v; n &= ((w) - 1);                                               \
        return n ? (uint32_t)(type)((x << n) | (x >> ((w) - n))) : v; }                 \
    static inline uint32_t op_ror##w(uint32_t v, unsigned n) {                          \
        type x = (type)v; n &= ((w) - 1);                                               \
        return n ? (uint32_t)(type)((x >> n) | (x << ((w) - n))) : v; }

DEF_SHIFT(8,  uint8_t)
DEF_SHIFT(16, uint16_t)
DEF_SHIFT(32, uint32_t)

#undef DEF_SHIFT

/* ---- x87, evaluated in host double (see docs/isa-scope.md) ---- */

static inline double LDF32(uint32_t a) { float f;  __builtin_memcpy(&f, g_mem + a, 4); return f; }
static inline double LDF64(uint32_t a) { double d; __builtin_memcpy(&d, g_mem + a, 8); return d; }
static inline void STF32(uint32_t a, double v) { float f = (float)v; __builtin_memcpy(g_mem + a, &f, 4); }
static inline void STF64(uint32_t a, double v) { __builtin_memcpy(g_mem + a, &v, 8); }

#define FST(i) (cpu.st[(cpu.st_top + (i)) & 7])

static inline void fpu_push(double v) { cpu.st_top = (cpu.st_top - 1) & 7; cpu.st[cpu.st_top] = v; }
static inline double fpu_pop(void) { double v = cpu.st[cpu.st_top]; cpu.st_top = (cpu.st_top + 1) & 7; return v; }

/* Sets C3/C2/C0 in the status word, which is how this binary compares floats --
 * it predates FCOMI, so every compare goes FCOM -> FNSTSW AX -> TEST AH. */
static inline void fpu_cmp(double a, double b)
{
    cpu.fsw &= (uint16_t)~0x4500u;
    if (a < b)       cpu.fsw |= 0x0100;      /* C0 */
    else if (a == b) cpu.fsw |= 0x4000;      /* C3 */
    else if (!(a == a) || !(b == b)) cpu.fsw |= 0x4500;  /* unordered */
}

/* ---- string ops ----
 * The binary contains no STD/CLD, so the direction flag is never set and these only
 * ever run forward. If that ever stops being true this must grow a DF check. */
void op_movs(int size, int rep);
void op_stos(int size, int rep);
void op_lods(int size);
void op_cmps(int size, int repe);
void op_scas(int size, int repe);
void op_cpuid(void);

/* Function-entry tracing. Off unless the generated code is built with LF2_FN_TRACE,
 * so the normal build pays nothing; the ring is dumped alongside the call trace when a
 * dispatch fails, which gives the path into a fault rather than just its location. */
#ifdef LF2_FN_TRACE
void fn_enter(uint32_t addr);
#define FN_ENTER(a) fn_enter(a)
#else
#define FN_ENTER(a) ((void)0)
#endif

/* Instructions the lifter does not emit yet. Aborts loudly rather than silently doing
 * nothing -- a no-op here would look like a working port that quietly computes garbage. */
#define TODO(what)                                                                      \
    do {                                                                                \
        fprintf(stderr, "unimplemented opcode %s at eip=%08x\n", (what), cpu.eip);       \
        abort();                                                                        \
    } while (0)

#endif /* GUEST_OPS_H */
