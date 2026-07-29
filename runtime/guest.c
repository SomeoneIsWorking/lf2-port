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

    /* Map the headers too. A real loader maps SizeOfHeaders bytes at the image base, and
     * this program depends on it: it checks the MZ signature at 0x400000, and its
     * resources are found by walking the data directory in mapped memory. */
    const uint32_t hdr_size = *(uint32_t *)(file + pe + 24 + 60);
    memcpy(g_mem + base, file, hdr_size ? hdr_size : 0x400);

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
Handler gdi_lookup(const char *dll, const char *name);
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
    if (!h) h = gdi_lookup(imports[i].dll, imports[i].name);
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
static uint32_t trace[TRACE_N], trace_esp[TRACE_N];
static unsigned trace_pos;

enum { FN_TRACE_N = 40 };
static uint32_t fn_ring[FN_TRACE_N];
static unsigned fn_pos;

void fn_enter(uint32_t addr)
{
    fn_ring[fn_pos % FN_TRACE_N] = addr;
    fn_pos++;

    /* LF2_FN_WATCH=<hex addr>: dump the caller and stack arguments on entry. The hook
     * runs before the prologue, so [ESP] is the return address and args follow it. */
    static uint32_t watch;
    static int armed;
    if (!armed) {
        const char *w = getenv("LF2_FN_WATCH");
        watch = w ? (uint32_t)strtoul(w, NULL, 16) : 0;
        armed = 1;
    }
    if (watch && addr == watch) {
        fprintf(stderr, "enter %08x from %08x  ecx=%08x args:", addr, LD32(R(ESP)), R(ECX));
        for (int i = 0; i < 6; i++) fprintf(stderr, " %08x", LD32(R(ESP) + 4 + 4 * (unsigned)i));
        fprintf(stderr, "\n");
    }
}

static void dump_fn_trace(void)
{
    if (!fn_pos) return;
    fprintf(stderr, "guest functions entered (newest last):");
    for (unsigned i = 0; i < FN_TRACE_N; i++) {
        uint32_t a = fn_ring[(fn_pos + i) % FN_TRACE_N];
        if (a) fprintf(stderr, " %08x", a);
    }
    fprintf(stderr, "\n");
}

static void dump_trace(void)
{
    static const char *RN[8] = { "eax","ecx","edx","ebx","esp","ebp","esi","edi" };
    fprintf(stderr, "regs:");
    for (int i = 0; i < 8; i++) fprintf(stderr, " %s=%08x", RN[i], R(i));
    fprintf(stderr, "\n");
    dump_fn_trace();
    fprintf(stderr, "recent calls (newest last):");
    for (unsigned i = 0; i < TRACE_N; i++) {
        unsigned k = (trace_pos + i) % TRACE_N;
        if (trace[k]) fprintf(stderr, " %08x(esp=%08x)", trace[k], trace_esp[k]);
    }
    fprintf(stderr, "\n");
}

void dispatch(uint32_t target)
{
    /* ESP must stay inside the stack. A wrong stdcall pop count walks it out of the
     * region, and the next CALL then writes its return address over whatever is there
     * -- which is how a corrupted import table showed up far from the real fault. */
    /* Watchpoint: report the first time the watched dword changes, with the guest
     * return address of the call we are in -- that localises the writer. */
    {
        static uint32_t watch_addr, watch_val;
        static int watch_armed;
        const char *w = getenv("LF2_WATCH");
        if (w && !watch_armed) {
            watch_addr = (uint32_t)strtoul(w, NULL, 16);
            watch_val = LD32(watch_addr);
            watch_armed = 1;
            fprintf(stderr, "watching %08x = %08x\n", watch_addr, watch_val);
        }
        if (watch_armed && LD32(watch_addr) != watch_val) {
            fprintf(stderr, "WATCH %08x changed %08x -> %08x; now calling %08x, ret %08x\n",
                    watch_addr, watch_val, LD32(watch_addr), target, LD32(R(ESP)));
            watch_val = LD32(watch_addr);
            dump_trace();
        }
    }

    if (R(ESP) < STACK_TOP - STACK_SIZE || R(ESP) > STACK_TOP) {
        fprintf(stderr, "ESP out of range: %08x calling %08x from guest %08x\n",
                R(ESP), target, LD32(R(ESP)));
        dump_trace();
        abort();
    }
    trace[trace_pos % TRACE_N] = target;
    trace_esp[trace_pos % TRACE_N] = R(ESP);
    trace_pos++;

    if (target >= 0xF1000000u && target < 0xF2000000u) { com_call(target); return; }
    if (target >= IMPORT_SENTINEL) { host_import(target); return; }
    const GuestFunc *f = bsearch(&target, g_funcs, (size_t)g_nfuncs, sizeof g_funcs[0], cmp_addr);
    if (!f) {
        fprintf(stderr, "indirect call to unknown address %08x from guest %08x\n",
                target, LD32(R(ESP)));
        dump_trace();
        abort();
    }
    cpu.eip = target;
    f->fn();
}
