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

/* Instructions the lifter does not emit yet. Aborts loudly rather than silently doing
 * nothing -- a no-op here would look like a working port that quietly computes garbage. */
#define TODO(what)                                                                      \
    do {                                                                                \
        fprintf(stderr, "unimplemented opcode %s at eip=%08x\n", (what), cpu.eip);       \
        abort();                                                                        \
    } while (0)

#endif /* GUEST_OPS_H */
