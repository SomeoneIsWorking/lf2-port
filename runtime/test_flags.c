/* Check the lazy flag evaluation against the real thing.
 *
 * The host is x86, so it can compute reference flags for the same operation with the
 * same operands. A wrong flag sends a Jcc down the wrong path, which is indistinguishable
 * from a lifting bug at the crash site -- so this pins it down independently. */
#include "guest_ops.h"

#include <stdio.h>

/* The flag module needs the CPU record and guest memory; the test drives them
 * directly and never touches memory. */
Cpu cpu;
uint8_t *g_mem;

enum { CF = 1u << 0, PF = 1u << 2, ZF = 1u << 6, SF = 1u << 7, OF = 1u << 11 };

static long failures, checks;

static void compare(const char *what, uint32_t a, uint32_t b, uint32_t host)
{
    const struct { const char *name; unsigned bit; int got; } f[] = {
        { "CF", CF, flag_cf() }, { "ZF", ZF, flag_zf() },
        { "SF", SF, flag_sf() }, { "OF", OF, flag_of() }, { "PF", PF, flag_pf() },
    };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        const int want = (host & f[i].bit) != 0;
        checks++;
        if (want != !!f[i].got) {
            if (failures < 20)
                fprintf(stderr, "%-14s a=%08x b=%08x  %s: want %d got %d\n",
                        what, a, b, f[i].name, want, !!f[i].got);
            failures++;
        }
    }
}

#define BINOP32(name, insn, kind)                                                  \
    static void name(uint32_t a, uint32_t b)                                       \
    {                                                                              \
        uint32_t r = a; uint64_t fl;                                               \
        __asm__ volatile (insn " %2, %0; pushfq; popq %1"                          \
                          : "+r"(r), "=r"(fl) : "r"(b) : "cc");                    \
        FLAGS(kind, 4, a, b, r);                                                   \
        compare(insn "32", a, b, (uint32_t)fl);                                    \
    }

BINOP32(t_add, "addl", F_ADD)
BINOP32(t_sub, "subl", F_SUB)
BINOP32(t_and, "andl", F_LOGIC)
BINOP32(t_or,  "orl",  F_LOGIC)
BINOP32(t_xor, "xorl", F_LOGIC)

static void t_cmp(uint32_t a, uint32_t b)
{
    uint64_t fl;
    __asm__ volatile ("cmpl %2, %1; pushfq; popq %0" : "=r"(fl) : "r"(a), "r"(b) : "cc");
    FLAGS(F_SUB, 4, a, b, a - b);
    compare("cmpl32", a, b, (uint32_t)fl);
}

static void t_inc(uint32_t a)
{
    uint32_t r = a; uint64_t fl;
    __asm__ volatile ("incl %0; pushfq; popq %1" : "+r"(r), "=r"(fl) :: "cc");
    FLAGS(F_INC, 4, a, 1, r);
    /* INC leaves CF untouched, so it is not comparable here. */
    const uint32_t host = (uint32_t)fl;
    checks += 4;
    if (!!flag_zf() != !!(host & ZF)) { fprintf(stderr, "inc a=%08x ZF\n", a); failures++; }
    if (!!flag_sf() != !!(host & SF)) { fprintf(stderr, "inc a=%08x SF\n", a); failures++; }
    if (!!flag_of() != !!(host & OF)) { fprintf(stderr, "inc a=%08x OF\n", a); failures++; }
    if (!!flag_pf() != !!(host & PF)) { fprintf(stderr, "inc a=%08x PF\n", a); failures++; }
}

static void t_dec(uint32_t a)
{
    uint32_t r = a; uint64_t fl;
    __asm__ volatile ("decl %0; pushfq; popq %1" : "+r"(r), "=r"(fl) :: "cc");
    FLAGS(F_DEC, 4, a, 1, r);
    const uint32_t host = (uint32_t)fl;
    checks += 4;
    if (!!flag_zf() != !!(host & ZF)) { fprintf(stderr, "dec a=%08x ZF\n", a); failures++; }
    if (!!flag_sf() != !!(host & SF)) { fprintf(stderr, "dec a=%08x SF\n", a); failures++; }
    if (!!flag_of() != !!(host & OF)) { fprintf(stderr, "dec a=%08x OF want %d got %d\n",
                                                a, !!(host & OF), !!flag_of()); failures++; }
    if (!!flag_pf() != !!(host & PF)) { fprintf(stderr, "dec a=%08x PF\n", a); failures++; }
}

static void t_sub8(uint32_t a, uint32_t b)
{
    uint8_t r = (uint8_t)a; uint64_t fl;
    __asm__ volatile ("subb %2, %0; pushfq; popq %1"
                      : "+q"(r), "=r"(fl) : "q"((uint8_t)b) : "cc");
    FLAGS(F_SUB, 1, a & 0xff, b & 0xff, (uint32_t)((a - b) & 0xff));
    compare("subb8", a & 0xff, b & 0xff, (uint32_t)fl);
}

static void t_shl(uint32_t a, unsigned n)
{
    if (!n) return;                       /* count 0 leaves flags untouched */
    uint32_t r = a; uint64_t fl;
    __asm__ volatile ("shll %%cl, %0; pushfq; popq %1"
                      : "+r"(r), "=r"(fl) : "c"(n) : "cc");
    (void)op_shl32(a, n);
    compare("shll32", a, n, (uint32_t)fl);
}

static void t_shr(uint32_t a, unsigned n)
{
    if (!n) return;
    uint32_t r = a; uint64_t fl;
    __asm__ volatile ("shrl %%cl, %0; pushfq; popq %1"
                      : "+r"(r), "=r"(fl) : "c"(n) : "cc");
    (void)op_shr32(a, n);
    compare("shrl32", a, n, (uint32_t)fl);
}

static void t_sar(uint32_t a, unsigned n)
{
    if (!n) return;
    uint32_t r = a; uint64_t fl;
    __asm__ volatile ("sarl %%cl, %0; pushfq; popq %1"
                      : "+r"(r), "=r"(fl) : "c"(n) : "cc");
    (void)op_sar32(a, n);
    compare("sarl32", a, n, (uint32_t)fl);
}

int main(void)
{
    static const uint32_t V[] = {
        0, 1, 2, 7, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
        0x7ffffffeu, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu,
        0x12345678u, 0xdeadbeefu, 0xa5a5a5a5u,
    };
    enum { N = sizeof V / sizeof V[0] };

    for (unsigned i = 0; i < N; i++) {
        t_inc(V[i]);
        t_dec(V[i]);
        for (unsigned n = 1; n < 32; n++) { t_shl(V[i], n); t_shr(V[i], n); t_sar(V[i], n); }
        for (unsigned j = 0; j < N; j++) {
            t_add(V[i], V[j]); t_sub(V[i], V[j]); t_and(V[i], V[j]);
            t_or(V[i], V[j]);  t_xor(V[i], V[j]); t_cmp(V[i], V[j]);
            t_sub8(V[i], V[j]);
        }
    }

    printf("%ld flag checks, %ld failures\n", checks, failures);
    return failures ? 1 : 0;
}
