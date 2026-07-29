/* Guest machine: flat memory, lazy flag evaluation, string helpers, image loading. */
#include "guest_ops.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

static void bind_imports(uint8_t *file, uint32_t base, uint32_t pe);

Cpu cpu;
uint8_t *g_mem;

enum { GUEST_SPACE = 0x100000000ull };   /* full 32-bit space, lazily committed */
enum { STACK_TOP = 0x00300000, STACK_SIZE = 0x00100000 };

void guest_init(void)
{
    /* Reserving the whole 4 GiB means a guest address is just an index -- no bounds
     * check or translation on the hot path. Pages are only committed when touched. */
    void *p = mmap(NULL, GUEST_SPACE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap guest space"); abort(); }
    g_mem = p;

    memset(&cpu, 0, sizeof cpu);
    cpu.st_top = 0;
    R(ESP) = STACK_TOP;
    R(EBP) = STACK_TOP;
}

void guest_load_image(const char *exe_path)
{
    FILE *fh = fopen(exe_path, "rb");
    if (!fh) { perror(exe_path); abort(); }
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    rewind(fh);
    uint8_t *file = malloc((size_t)n);
    if (fread(file, 1, (size_t)n, fh) != (size_t)n) { fprintf(stderr, "short read\n"); abort(); }
    fclose(fh);

    const uint32_t pe = *(uint32_t *)(file + 0x3C);
    const uint16_t nsec = *(uint16_t *)(file + pe + 6);
    const uint16_t optsz = *(uint16_t *)(file + pe + 20);
    const uint32_t base = *(uint32_t *)(file + pe + 24 + 28);
    const uint8_t *sec = file + pe + 24 + optsz;

    for (int i = 0; i < nsec; i++) {
        const uint8_t *s = sec + i * 40;
        const uint32_t vsize = *(uint32_t *)(s + 8);
        const uint32_t rva   = *(uint32_t *)(s + 12);
        const uint32_t rsize = *(uint32_t *)(s + 16);
        const uint32_t roff  = *(uint32_t *)(s + 20);
        memset(g_mem + base + rva, 0, vsize);
        memcpy(g_mem + base + rva, file + roff, rsize < vsize ? rsize : vsize);
    }
    bind_imports(file, base, pe);
    free(file);
}

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

/* ---- imports ----
 * Each IAT slot is overwritten with a sentinel so an indirect call through it arrives
 * in dispatch() identifiable. Unimplemented ones name themselves instead of jumping
 * into unmapped memory. */
#define MAX_IMPORTS 512
static struct { char dll[32]; char name[64]; } imports[MAX_IMPORTS];
static int nimports;

static void bind_imports(uint8_t *file, uint32_t base, uint32_t pe)
{
    const uint32_t dir = *(uint32_t *)(file + pe + 24 + 104);
    if (!dir) return;
    uint8_t *d = g_mem + base + dir;
    for (;; d += 20) {
        const uint32_t oft = *(uint32_t *)(d + 0), name_rva = *(uint32_t *)(d + 12);
        const uint32_t fta = *(uint32_t *)(d + 16);
        if (!name_rva) break;
        const char *dll = (const char *)(g_mem + base + name_rva);
        uint32_t *thunk = (uint32_t *)(g_mem + base + (oft ? oft : fta));
        uint32_t *iat   = (uint32_t *)(g_mem + base + fta);
        for (int i = 0; thunk[i]; i++) {
            if (nimports >= MAX_IMPORTS) break;
            snprintf(imports[nimports].dll, sizeof imports[0].dll, "%s", dll);
            if (thunk[i] & 0x80000000u)
                snprintf(imports[nimports].name, sizeof imports[0].name, "#%u", thunk[i] & 0xffff);
            else
                snprintf(imports[nimports].name, sizeof imports[0].name, "%s",
                         (const char *)(g_mem + base + thunk[i] + 2));
            iat[i] = IMPORT_SENTINEL + (uint32_t)nimports;
            nimports++;
        }
    }
}

typedef void (*Handler)(void);
Handler host_lookup(const char *dll, const char *name);
Handler win32_lookup(const char *dll, const char *name);
Handler gfx_lookup(const char *dll, const char *name);
Handler dsound_lookup(const char *dll, const char *name);
void com_call(uint32_t sentinel);

void host_import(uint32_t sentinel)
{
    const uint32_t i = sentinel - IMPORT_SENTINEL;
    if (i >= (uint32_t)nimports) {
        fprintf(stderr, "call to unknown import sentinel %08x\n", sentinel);
        abort();
    }
    Handler h = host_lookup(imports[i].dll, imports[i].name);
    if (!h) h = win32_lookup(imports[i].dll, imports[i].name);
    if (!h) h = gfx_lookup(imports[i].dll, imports[i].name);
    if (!h) h = dsound_lookup(imports[i].dll, imports[i].name);
    if (!h) {
        fprintf(stderr, "unimplemented import: %s.%s\n", imports[i].dll, imports[i].name);
        abort();
    }
    h();
}

static int cmp_addr(const void *k, const void *e)
{
    const uint32_t a = *(const uint32_t *)k, b = ((const GuestFunc *)e)->addr;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* Ring of recent dispatch targets, dumped when a call fails -- an indirect call to a
 * bad address otherwise gives no clue where it came from. */
enum { TRACE_N = 24 };
static uint32_t trace[TRACE_N];
static unsigned trace_pos;

static void dump_trace(void)
{
    fprintf(stderr, "recent calls (newest last):");
    for (unsigned i = 0; i < TRACE_N; i++) {
        uint32_t t = trace[(trace_pos + i) % TRACE_N];
        if (t) fprintf(stderr, " %08x", t);
    }
    fprintf(stderr, "\n");
}

void dispatch(uint32_t target)
{
    trace[trace_pos % TRACE_N] = target;
    trace_pos++;

    if (target >= 0xF1000000u && target < 0xF2000000u) { com_call(target); return; }
    if (target >= IMPORT_SENTINEL) { host_import(target); return; }
    const GuestFunc *f = bsearch(&target, g_funcs, (size_t)g_nfuncs, sizeof g_funcs[0], cmp_addr);
    if (!f) {
        fprintf(stderr, "indirect call to unknown address %08x\n", target);
        dump_trace();
        abort();
    }
    cpu.eip = target;
    f->fn();
}
