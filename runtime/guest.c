/* Guest machine: flat memory, lazy flag evaluation, string helpers, image loading. */
#include "guest_ops.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

/* macOS has no MAP_NORESERVE -- it never over-commits the way Linux does, so the flag is
 * only ever a hint and dropping it changes nothing. */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

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
    cpu.fcw = 0x027F;      /* MSVC default: exceptions masked, 53-bit precision */
    R(ESP) = STACK_TOP;
    R(EBP) = STACK_TOP;

    /* Minimal TIB. The SEH chain head must terminate properly or the CRT prologues
     * build their frames from garbage. */
    ST32(TIB_BASE + 0x00, 0xFFFFFFFFu);          /* ExceptionList: end of chain */
    ST32(TIB_BASE + 0x04, STACK_TOP);            /* StackBase */
    ST32(TIB_BASE + 0x08, STACK_TOP - STACK_SIZE); /* StackLimit */
    ST32(TIB_BASE + 0x18, TIB_BASE);             /* Self */
    ST32(TIB_BASE + 0x24, 0x5678);               /* thread id */
    ST32(TIB_BASE + 0x30, TIB_BASE + 0x1000);    /* PEB */
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
Handler gamepad_lookup(const char *dll, const char *name);
Handler wsock_lookup(const char *dll, const char *name);
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
    if (!h) h = gamepad_lookup(imports[i].dll, imports[i].name);
    if (!h) h = wsock_lookup(imports[i].dll, imports[i].name);
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

/* Sampling the watch at every function entry rather than only at indirect calls: a
 * value written and consumed between two indirect calls is invisible otherwise. */
void watch_sample(const char *where, uint32_t ctx);
void watch_arm(uint32_t a);
extern int esp_log_active;

/* LF2_DUMP_MEM=addr:count -- print guest dwords once, for reading the game's own tables. */
void dump_mem_once(void)
{
    static int done;
    const char *spec = getenv("LF2_DUMP_MEM");
    if (done || !spec) return;
    done = 1;
    while (spec && *spec) {
        char *e;
        const uint32_t a = (uint32_t)strtoul(spec, &e, 16);
        unsigned n = (*e == ':') ? (unsigned)strtoul(e + 1, &e, 10) : 8;
        fprintf(stderr, "mem %08x:", a);
        for (unsigned i = 0; i < n; i++) fprintf(stderr, " %d", (int)LD32(a + i * 4));
        fprintf(stderr, "\n");
        while (*e == ',' || *e == ' ') e++;
        spec = *e ? e : NULL;
    }
}

void probe(uint32_t addr)
{
    fprintf(stderr, "PROBE %08x eax=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n", addr, R(EAX), R(ECX), R(EDX), R(ESI), R(EDI));
}

void fn_enter(uint32_t addr)
{
    fn_ring[fn_pos % FN_TRACE_N] = addr;
    fn_pos++;
    watch_sample("fn", addr);

    /* LF2_FN_WATCH=<hex addr>: dump the caller and stack arguments on entry. The hook
     * runs before the prologue, so [ESP] is the return address and args follow it. */
    static uint32_t watch;
    static int armed;
    if (!armed) {
        const char *w = getenv("LF2_FN_WATCH");
        watch = w ? (uint32_t)strtoul(w, NULL, 16) : 0;
        armed = 1;
    }
    /* Arm the memory watch on a slot in this function's frame. fn_enter runs before the
     * prologue, so ESP still points at the return address and the offset is relative to
     * that. This is how a clobbered local gets attributed to its writer. */
    if (watch && addr == watch) {
        if (getenv("LF2_ESP_LOG")) esp_log_active = 1;
        const char *rel = getenv("LF2_WATCH_REL");
        if (rel) {
            const long off = strtol(rel, NULL, 0);
            watch_arm((uint32_t)((int64_t)R(ESP) + off));
        }
    }
    if (watch && addr == watch) {
        fprintf(stderr, "enter %08x from %08x  ecx=%08x args:", addr, LD32(R(ESP)), R(ECX));
        for (int i = 0; i < 6; i++) fprintf(stderr, " %08x", LD32(R(ESP) + 4 + 4 * (unsigned)i));
        fprintf(stderr, "\n");
    }
}

void dump_fn_trace(void)
{
    if (!fn_pos) return;
    fprintf(stderr, "guest functions entered (newest last):");
    for (unsigned i = 0; i < FN_TRACE_N; i++) {
        uint32_t a = fn_ring[(fn_pos + i) % FN_TRACE_N];
        if (a) fprintf(stderr, " %08x", a);
    }
    fprintf(stderr, "\n");
}

void dump_trace(void)
{
    static const char *RN[8] = { "eax","ecx","edx","ebx","esp","ebp","esi","edi" };
    fprintf(stderr, "regs:");
    for (int i = 0; i < 8; i++) fprintf(stderr, " %s=%08x", RN[i], R(i));
    fprintf(stderr, "\n");
    /* Raw stack around ESP: hand-computing frame offsets from the disassembly has been
     * unreliable, and the actual bytes settle which slot the guest read. */
    fprintf(stderr, "stack:\n");
    for (int row = -2; row < 10; row++) {
        const uint32_t a = R(ESP) + (uint32_t)(row * 16);
        fprintf(stderr, "  %08x:", a);
        for (int i = 0; i < 4; i++) fprintf(stderr, " %08x", LD32(a + (uint32_t)i * 4));
        fprintf(stderr, "%s\n", row == 0 ? "   <- esp" : "");
    }
    /* Where does the bad value actually live? Scan the stack rather than deriving the
     * frame offset from the disassembly, which has been wrong more than once. */
    {
        const uint32_t bad = LD32(R(ESP) + 4);
        fprintf(stderr, "occurrences of %08x on the stack:", bad);
        int hits = 0;
        for (uint32_t a = STACK_TOP - STACK_SIZE; a < STACK_TOP && hits < 12; a += 4)
            if (LD32(a) == bad) { fprintf(stderr, " %08x", a); hits++; }
        fprintf(stderr, "%s\n", hits ? "" : " none");
    }
    /* Which of the game's variables still hold valid COM object pointers? The correct
     * destination is one of these; seeing which are intact says whether the game lost it
     * or never stored it. */
    {
        fprintf(stderr, "COM pointers in the image data:\n");
        int shown = 0;
        for (uint32_t a = 0x400000; a < 0x460000 && shown < 16; a += 4) {
            const uint32_t v = LD32(a);
            if (v >= 0x30000000u && v < 0x30010000u) {
                fprintf(stderr, "  [%08x] = %08x\n", a, v);
                shown++;
            }
        }
        if (!shown) fprintf(stderr, "  none\n");
    }
    dump_fn_trace();
    fprintf(stderr, "recent calls (newest last):");
    for (unsigned i = 0; i < TRACE_N; i++) {
        unsigned k = (trace_pos + i) % TRACE_N;
        if (trace[k]) fprintf(stderr, " %08x(esp=%08x)", trace[k], trace_esp[k]);
    }
    fprintf(stderr, "\n");
}

void stack_check(uint32_t esp_at_entry, uint32_t fn)
{
    if (R(ESP) == esp_at_entry) return;
    /* Report each offending function once and keep going: the first imbalance is not
     * necessarily the damaging one, and the set is more informative than the earliest. */
    enum { SEEN_MAX = 64 };
    static uint32_t seen[SEEN_MAX];
    static int nseen;
    for (int i = 0; i < nseen; i++) if (seen[i] == fn) return;
    if (nseen < SEEN_MAX) seen[nseen++] = fn;
    fprintf(stderr, "STACK IMBALANCE fn_%08x: %+d bytes\n",
            fn, (int)(R(ESP) - esp_at_entry));
}

static uint32_t w_addr, w_val, w_target;
static int w_armed;

void watch_arm(uint32_t a)
{
    w_addr = a;
    w_val = LD32(a);
    w_armed = 1;
    fprintf(stderr, "watch armed at %08x = %08x\n", a, w_val);
}

void watch_sample(const char *where, uint32_t ctx)
{
    static uint32_t addr, val, target;
    static int armed;
    if (w_armed) {
        const uint32_t now = LD32(w_addr);
        if (now != w_val) {
            fprintf(stderr, "SLOT %08x: %08x -> %08x at %s %08x\n",
                    w_addr, w_val, now, where, ctx);
            w_val = now;
        }
        return;
    }
    if (!armed) {
        const char *w = getenv("LF2_WATCH");
        const char *t = getenv("LF2_WATCH_VAL");
        addr = w ? (uint32_t)strtoul(w, NULL, 16) : 0;
        target = t ? (uint32_t)strtoul(t, NULL, 16) : 0;
        if (addr) val = LD32(addr);
        armed = 1;
    }
    if (!addr) return;
    const uint32_t now = LD32(addr);
    if (now == val) return;
    if (!target || now == target) {
        fprintf(stderr, "WATCH %08x: %08x -> %08x at %s %08x (esp=%08x)\n",
                addr, val, now, where, ctx, R(ESP));
        dump_fn_trace();
    }
    val = now;
}

/* Set once the function under investigation is entered, so the ESP log covers only its
 * execution instead of the whole run. */
int esp_log_active;

void dispatch(uint32_t target)
{
    /* Host handlers pop their own stdcall arguments, so the delta across one is the only
     * place a wrong count can show. Guest functions are already covered by STACK_CHECK. */
    if (esp_log_active && target >= 0xF0000000u) {
        const uint32_t before = R(ESP);
        if (target >= 0xF1000000u && target < 0xF2000000u) com_call(target);
        else host_import(target);
        fprintf(stderr, "ESP %08x -> %08x (%+d) %08x\n",
                before, R(ESP), (int)(R(ESP) - before), target);
        return;
    }

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
        static uint32_t watch_target;
        static int target_set;
        if (!target_set) {
            const char *t = getenv("LF2_WATCH_VAL");
            watch_target = t ? (uint32_t)strtoul(t, NULL, 16) : 0;
            target_set = 1;
        }
        if (watch_armed && LD32(watch_addr) != watch_val &&
            (!watch_target || LD32(watch_addr) == watch_target)) {
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
