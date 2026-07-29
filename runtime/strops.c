/* String operations, split out so the differential test can link them without the
 * dispatch and import machinery. */
#include "guest_ops.h"

/* ---- string ops (forward only; the binary has no STD/CLD) ---- */

static uint32_t ld(uint32_t a, int size)
{
    return size == 1 ? LD8(a) : size == 2 ? LD16(a) : LD32(a);
}
static void st(uint32_t a, int size, uint32_t v)
{
    if (size == 1) ST8(a, (uint8_t)v); else if (size == 2) ST16(a, (uint16_t)v); else ST32(a, v);
}

void op_movs(int size, int rep)
{
    do {
        if (rep && R(ECX) == 0) return;
        st(R(EDI), size, ld(R(ESI), size));
        R(ESI) += (uint32_t)size;
        R(EDI) += (uint32_t)size;
        if (rep) R(ECX)--;
    } while (rep && R(ECX));
}

void op_stos(int size, int rep)
{
    do {
        if (rep && R(ECX) == 0) return;
        st(R(EDI), size, R(EAX));
        R(EDI) += (uint32_t)size;
        if (rep) R(ECX)--;
    } while (rep && R(ECX));
}

void op_lods(int size)
{
    uint32_t v = ld(R(ESI), size);
    if (size == 1) SETR8(0, v); else if (size == 4) R(EAX) = v;
    else R(EAX) = (R(EAX) & ~0xffffu) | (v & 0xffffu);
    R(ESI) += (uint32_t)size;
}

/* repe > 0 = REPE, repe < 0 = REPNE, 0 = no prefix */
void op_cmps(int size, int repe)
{
    do {
        if (repe && R(ECX) == 0) return;
        uint32_t a = ld(R(ESI), size), b = ld(R(EDI), size);
        FLAGS(F_SUB, (uint8_t)size, a, b, a - b);
        R(ESI) += (uint32_t)size;
        R(EDI) += (uint32_t)size;
        if (repe) {
            R(ECX)--;
            if (repe > 0 && !flag_zf()) return;
            if (repe < 0 && flag_zf()) return;
        }
    } while (repe && R(ECX));
}

void op_scas(int size, int repe)
{
    do {
        if (repe && R(ECX) == 0) return;
        uint32_t a = size == 1 ? GETR8(0) : R(EAX), b = ld(R(EDI), size);
        FLAGS(F_SUB, (uint8_t)size, a, b, a - b);
        R(EDI) += (uint32_t)size;
        if (repe) {
            R(ECX)--;
            if (repe > 0 && !flag_zf()) return;
            if (repe < 0 && flag_zf()) return;
        }
    } while (repe && R(ECX));
}

void op_cpuid(void)
{
    /* Minimal, stable answer: the game only uses this for a feature probe. */
    switch (R(EAX)) {
    case 0:  R(EAX) = 1; R(EBX) = 0x756e6547; R(EDX) = 0x49656e69; R(ECX) = 0x6c65746e; break;
    case 1:  R(EAX) = 0x00000633; R(EBX) = 0; R(ECX) = 0; R(EDX) = 0x00000001 | (1u << 15) | (1u << 23) | (1u << 25) | (1u << 26); break;
    default: R(EAX) = R(EBX) = R(ECX) = R(EDX) = 0; break;
    }
}

