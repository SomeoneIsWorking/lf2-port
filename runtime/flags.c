/* Lazy flag evaluation, split out so it can be unit-tested against the host CPU
 * without dragging in image loading and dispatch. */
#include "guest.h"

/* ---- lazy flags ----
 * Arithmetic records its operands and result; a flag is derived only when read.
 * Only PUSHFD/POPFD (5 sites in the binary) need the whole register at once. */

static uint32_t msb(void) { return 1u << (cpu.size * 8 - 1); }
static uint32_t mask(void) { return cpu.size == 4 ? 0xffffffffu : (1u << (cpu.size * 8)) - 1; }

int flag_zf(void) { return (cpu.res & mask()) == 0; }
int flag_sf(void) { return (cpu.res & msb()) != 0; }

int flag_cf(void)
{
    switch (cpu.op) {
    case F_ADD:   return (cpu.res & mask()) < (cpu.a & mask());
    case F_SUB:   return (cpu.a & mask()) < (cpu.b & mask());
    case F_LOGIC: return 0;
    case F_SHL:   return cpu.b ? ((cpu.a >> (cpu.size * 8 - cpu.b)) & 1) : 0;
    case F_SHR:
    case F_SAR:   return cpu.b ? ((cpu.a >> (cpu.b - 1)) & 1) : 0;
    default:      return cpu.cf_hint & 1;
    }
}

int flag_of(void)
{
    const uint32_t m = msb();
    switch (cpu.op) {
    case F_ADD: return (~(cpu.a ^ cpu.b) & (cpu.a ^ cpu.res) & m) != 0;
    case F_SUB: return (((cpu.a ^ cpu.b) & (cpu.a ^ cpu.res)) & m) != 0;
    case F_INC: return (cpu.res & mask()) == m;
    case F_DEC: return (cpu.res & mask()) == (m - 1);
    /* Shifts define OF only for a count of 1; the hardware still computes it, so match
     * what it does: SHL is result-MSB xor CF, SHR is the original MSB, SAR is always 0. */
    case F_SHL: return ((cpu.res & m) != 0) != (flag_cf() != 0);
    case F_SHR: return (cpu.b == 1) ? ((cpu.a & m) != 0) : 0;
    case F_SAR: return 0;
    default:    return 0;
    }
}

int flag_pf(void)
{
    uint8_t v = (uint8_t)cpu.res;
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return !(v & 1);
}

/* AF is not derivable from the lazy record for every op, but the binary never branches
 * on it -- no JA-family instruction reads it and there is no DAA/DAS/AAA/AAS. */
static int flag_af(void) { return ((cpu.a ^ cpu.b ^ cpu.res) & 0x10) != 0; }

uint32_t flags_pack(void)
{
    return 0x202u                       /* reserved bit 1, IF */
         | (uint32_t)(flag_cf() << 0)
         | (uint32_t)(flag_pf() << 2)
         | (uint32_t)(flag_af() << 4)
         | (uint32_t)(flag_zf() << 6)
         | (uint32_t)(flag_sf() << 7)
         | (uint32_t)(flag_of() << 11);
}

void flags_unpack(uint32_t v)
{
    /* Re-express the restored bits as a logic result so the lazy readers agree. */
    cpu.op = F_NONE;
    cpu.size = 4;
    cpu.a = cpu.b = 0;
    cpu.res = (v & 0x40) ? 0 : 1;       /* ZF */
    cpu.cf_hint = v & 1;
    if (v & 0x80) cpu.res |= 0x80000000u;
}

