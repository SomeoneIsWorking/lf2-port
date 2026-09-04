/* LF2 guest image/memory mapping and host-service interception. */
#include "lf2_log.h"
#include "environment.h"
#include "guest.h"
#include "jit_executor.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

/* macOS has no MAP_NORESERVE -- it never over-commits the way Linux does, so the flag is
 * only ever a hint and dropping it changes nothing. */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

static void bind_imports(uint8_t *file, uint32_t base, uint32_t pe);
static void dump_trace(void);

Cpu cpu;
uint8_t *g_mem;

/* The mapped extent of the loaded image. A memory scan that wants to say "I looked at all
 * of .text/.rdata/.data" has to know where they end; guessing a round number would make
 * every negative result carry an unstated blind spot. */
uint32_t g_image_lo, g_image_hi;

enum { GUEST_SPACE = 0x100000000ull }; /* full 32-bit space, lazily committed */
enum { STACK_TOP = 0x00300000, STACK_SIZE = 0x00100000 };

void guest_init(void)
{
    /* Reserving the whole 4 GiB means a guest address is just an index -- no bounds
     * check or translation on the hot path. Pages are only committed when touched. */
    void *p = mmap(NULL, GUEST_SPACE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        lf2_log_perror("guest", "mmap guest space");
        abort();
    }
    g_mem = p;

    memset(&cpu, 0, sizeof cpu);
    extern void (*rwatch_trace_hook)(void);
    rwatch_trace_hook = dump_trace;
    rwatch_init();
    R(ESP) = STACK_TOP;
    R(EBP) = STACK_TOP;

    /* Minimal TIB. The SEH chain head must terminate properly or the CRT prologues
     * build their frames from garbage. */
    ST32(TIB_BASE + 0x00, 0xFFFFFFFFu);            /* ExceptionList: end of chain */
    ST32(TIB_BASE + 0x04, STACK_TOP);              /* StackBase */
    ST32(TIB_BASE + 0x08, STACK_TOP - STACK_SIZE); /* StackLimit */
    ST32(TIB_BASE + 0x18, TIB_BASE);               /* Self */
    ST32(TIB_BASE + 0x24, 0x5678);                 /* thread id */
    ST32(TIB_BASE + 0x30, TIB_BASE + 0x1000);      /* PEB */
}

void guest_load_image(const char *exe_path)
{
    FILE *fh = fopen(exe_path, "rb");
    if (!fh) {
        lf2_log_perror("guest", exe_path);
        abort();
    }
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    rewind(fh);
    uint8_t *file = malloc((size_t)n);
    if (fread(file, 1, (size_t)n, fh) != (size_t)n) {
        lf2_log_writef(LF2_LOG_INFO, "guest", "short read\n");
        abort();
    }
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

    g_image_lo = base;
    g_image_hi = base + (hdr_size ? hdr_size : 0x400);

    for (int i = 0; i < nsec; i++) {
        const uint8_t *s = sec + i * 40;
        const uint32_t vsize = *(uint32_t *)(s + 8);
        const uint32_t rva = *(uint32_t *)(s + 12);
        const uint32_t rsize = *(uint32_t *)(s + 16);
        const uint32_t roff = *(uint32_t *)(s + 20);
        memset(g_mem + base + rva, 0, vsize);
        memcpy(g_mem + base + rva, file + roff, rsize < vsize ? rsize : vsize);
        if (base + rva + vsize > g_image_hi) g_image_hi = base + rva + vsize;
    }
    bind_imports(file, base, pe);
    free(file);
}

/* ---- imports ----
 * Each IAT slot is overwritten with a sentinel so an indirect call through it arrives
 * in dispatch() identifiable. Unimplemented ones name themselves instead of jumping
 * into unmapped memory. */
#define MAX_IMPORTS 512
static struct {
    char dll[32];
    char name[64];
} imports[MAX_IMPORTS];
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
        uint32_t *iat = (uint32_t *)(g_mem + base + fta);
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

/* LF2_IMPORT_STATS=1 reports the most-called imports at exit. A frequently-called no-op
 * stub and a correctly implemented function are indistinguishable from the outside, so
 * knowing which stubs are hot is the only way to tell which ones matter. */
static long import_calls[MAX_IMPORTS];
static Handler import_handler[MAX_IMPORTS];
/* Total wall time inside import handlers. Measured under LF2_IMPORT_STATS so the two
 * clock_gettime calls (tens of ns against ~7M calls) are not paid on a normal run. */
static double import_ns;
static double import_ns_each[MAX_IMPORTS];
static int import_timing = -1; /* resolved once; -1 = not yet checked */

void import_stats_report(void)
{
    if (!lf2_environment_get(LF2_ENV_IMPORT_STATS)) return;
    int idx[MAX_IMPORTS], n = nimports;
    for (int k = 0; k < n; k++) idx[k] = k;
    for (int a = 0; a < n; a++) /* selection sort; n is ~130 */
        for (int b = a + 1; b < n; b++)
            if (import_calls[idx[b]] > import_calls[idx[a]]) {
                const int t = idx[a];
                idx[a] = idx[b];
                idx[b] = t;
            }
    long total = 0;
    for (int k = 0; k < n; k++) total += import_calls[k];
    lf2_log_writef(LF2_LOG_INFO, "guest", "import calls: %ld total across %d imports\n", total, n);
    lf2_log_writef(LF2_LOG_INFO, "guest",
                   "import time:  %.3f s inside handlers, %.0f ns/call (timer overhead included)\n", import_ns / 1e9,
                   total ? import_ns / (double)total : 0.0);
    for (int k = 0; k < n && k < 12; k++) {
        if (!import_calls[idx[k]]) break;
        lf2_log_writef(LF2_LOG_INFO, "guest", "  %-34s %8ld\n", imports[idx[k]].name, import_calls[idx[k]]);
    }

    /* Ranked by TIME, not by count. Sleep is called rarely and blocks for milliseconds,
     * while fscanf is called millions of times and returns in nanoseconds -- the two
     * rankings disagree completely, and only this one says where the load goes. */
    for (int k = 0; k < n; k++) idx[k] = k;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (import_ns_each[idx[b]] > import_ns_each[idx[a]]) {
                const int t = idx[a];
                idx[a] = idx[b];
                idx[b] = t;
            }
    lf2_log_writef(LF2_LOG_INFO, "guest", "import time by handler:\n");
    for (int k = 0; k < n && k < 12; k++) {
        if (import_ns_each[idx[k]] <= 0) break;
        lf2_log_writef(LF2_LOG_INFO, "guest", "  %-34s %8.3f s  %8ld calls  %8.0f ns/call\n", imports[idx[k]].name,
                       import_ns_each[idx[k]] / 1e9, import_calls[idx[k]],
                       import_ns_each[idx[k]] / (double)import_calls[idx[k]]);
    }
}

void host_import(uint32_t sentinel)
{
    const uint32_t i = sentinel - IMPORT_SENTINEL;
    if (i >= (uint32_t)nimports) {
        lf2_log_writef(LF2_LOG_INFO, "guest", "call to unknown import sentinel %08x\n", sentinel);
        abort();
    }
    import_calls[i]++;
    if (import_timing < 0) import_timing = lf2_environment_get(LF2_ENV_IMPORT_STATS) != NULL;

    /* Resolved once per import, not once per call. The lookup walks up to seven tables
     * doing two strcmps per entry, and the game makes over seven million import calls in a
     * single load -- it decrypts each .dat to a temporary file and parses it back with
     * fscanf, so fscanf, feof and fprintf alone account for 6.9M of them. */
    Handler h = import_handler[i];
    if (!h) {
        h = host_lookup(imports[i].dll, imports[i].name);
        if (!h) h = win32_lookup(imports[i].dll, imports[i].name);
        if (!h) h = gfx_lookup(imports[i].dll, imports[i].name);
        if (!h) h = dsound_lookup(imports[i].dll, imports[i].name);
        if (!h) h = gdi_lookup(imports[i].dll, imports[i].name);
        if (!h) h = gamepad_lookup(imports[i].dll, imports[i].name);
        if (!h) h = wsock_lookup(imports[i].dll, imports[i].name);
        if (!h) {
            lf2_log_writef(LF2_LOG_INFO, "guest", "unimplemented import: %s.%s\n", imports[i].dll, imports[i].name);
            abort();
        }
        import_handler[i] = h;
    }
    if (!import_timing) {
        h();
        return;
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    h();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double d = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
    import_ns += d;
    import_ns_each[i] += d;
}

/* Ring of recent dispatch targets, dumped when a call fails -- an indirect call to a
 * bad address otherwise gives no clue where it came from. */
enum { TRACE_N = 24 };
static uint32_t trace[TRACE_N], trace_esp[TRACE_N];
static unsigned trace_pos;

/* LF2_DUMP_MEM=addr:count -- print guest dwords once, for reading the game's own tables. */
void dump_mem_once(void)
{
    static int done;
    const char *spec = lf2_environment_get(LF2_ENV_DUMP_MEM);
    if (done || !spec) return;
    done = 1;
    while (spec && *spec) {
        char *e;
        const uint32_t a = (uint32_t)strtoul(spec, &e, 16);
        unsigned n = (*e == ':') ? (unsigned)strtoul(e + 1, &e, 10) : 8;
        lf2_log_writef(LF2_LOG_INFO, "guest", "mem %08x:", a);
        for (unsigned i = 0; i < n; i++) lf2_log_writef(LF2_LOG_INFO, "guest", " %d", (int)LD32(a + i * 4));
        lf2_log_writef(LF2_LOG_INFO, "guest", "\n");
        while (*e == ',' || *e == ' ') e++;
        spec = *e ? e : NULL;
    }
}

static void dump_trace(void)
{
    static const char *RN[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    lf2_log_writef(LF2_LOG_INFO, "guest", "regs:");
    for (int i = 0; i < 8; i++) lf2_log_writef(LF2_LOG_INFO, "guest", " %s=%08x", RN[i], R(i));
    lf2_log_writef(LF2_LOG_INFO, "guest", "\n");
    /* Raw stack around ESP: hand-computing frame offsets from the disassembly has been
     * unreliable, and the actual bytes settle which slot the guest read. */
    lf2_log_writef(LF2_LOG_INFO, "guest", "stack:\n");
    for (int row = -2; row < 10; row++) {
        const uint32_t a = R(ESP) + (uint32_t)(row * 16);
        lf2_log_writef(LF2_LOG_INFO, "guest", "  %08x:", a);
        for (int i = 0; i < 4; i++) lf2_log_writef(LF2_LOG_INFO, "guest", " %08x", LD32(a + (uint32_t)i * 4));
        lf2_log_writef(LF2_LOG_INFO, "guest", "%s\n", row == 0 ? "   <- esp" : "");
    }
    /* Where does the bad value actually live? Scan the stack rather than deriving the
     * frame offset from the disassembly, which has been wrong more than once. */
    {
        const uint32_t bad = LD32(R(ESP) + 4);
        lf2_log_writef(LF2_LOG_INFO, "guest", "occurrences of %08x on the stack:", bad);
        int hits = 0;
        for (uint32_t a = STACK_TOP - STACK_SIZE; a < STACK_TOP && hits < 12; a += 4)
            if (LD32(a) == bad) {
                lf2_log_writef(LF2_LOG_INFO, "guest", " %08x", a);
                hits++;
            }
        lf2_log_writef(LF2_LOG_INFO, "guest", "%s\n", hits ? "" : " none");
    }
    /* Which of the game's variables still hold valid COM object pointers? The correct
     * destination is one of these; seeing which are intact says whether the game lost it
     * or never stored it. */
    {
        lf2_log_writef(LF2_LOG_INFO, "guest", "COM pointers in the image data:\n");
        int shown = 0;
        for (uint32_t a = 0x400000; a < 0x460000 && shown < 16; a += 4) {
            const uint32_t v = LD32(a);
            if (v >= 0x30000000u && v < 0x30010000u) {
                lf2_log_writef(LF2_LOG_INFO, "guest", "  [%08x] = %08x\n", a, v);
                shown++;
            }
        }
        if (!shown) lf2_log_writef(LF2_LOG_INFO, "guest", "  none\n");
    }
    const unsigned recorded = trace_pos < TRACE_N ? trace_pos : TRACE_N;
    lf2_log_writef(LF2_LOG_INFO, "guest", "recent calls (newest last, %u recorded):", recorded);
    for (unsigned i = 0; i < TRACE_N; i++) {
        unsigned k = (trace_pos + i) % TRACE_N;
        if (trace[k]) lf2_log_writef(LF2_LOG_INFO, "guest", " %08x(esp=%08x)", trace[k], trace_esp[k]);
    }
    lf2_log_writef(LF2_LOG_INFO, "guest", "%s\n", recorded ? "" : " none");
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
        static int watch_configured;
        if (!watch_configured) {
            const char *w = lf2_environment_get(LF2_ENV_WATCH);
            watch_configured = 1;
            if (!w) goto watch_ready;
            watch_addr = (uint32_t)strtoul(w, NULL, 16);
            watch_val = LD32(watch_addr);
            lf2_log_writef(LF2_LOG_INFO, "guest", "watching %08x = %08x\n", watch_addr, watch_val);
        }
    watch_ready:;
        static uint32_t watch_target;
        static int target_set;
        if (!target_set) {
            const char *t = lf2_environment_get(LF2_ENV_WATCH_VAL);
            watch_target = t ? (uint32_t)strtoul(t, NULL, 16) : 0;
            target_set = 1;
        }
        if (watch_addr && LD32(watch_addr) != watch_val && (!watch_target || LD32(watch_addr) == watch_target)) {
            lf2_log_writef(LF2_LOG_INFO, "guest", "WATCH %08x changed %08x -> %08x; now calling %08x, ret %08x\n",
                           watch_addr, watch_val, LD32(watch_addr), target, LD32(R(ESP)));
            watch_val = LD32(watch_addr);
            dump_trace();
        }
    }

    if (R(ESP) < STACK_TOP - STACK_SIZE || R(ESP) > STACK_TOP) {
        lf2_log_writef(LF2_LOG_INFO, "guest", "ESP out of range: %08x calling %08x from guest %08x\n", R(ESP), target,
                       LD32(R(ESP)));
        dump_trace();
        abort();
    }
    trace[trace_pos % TRACE_N] = target;
    trace_esp[trace_pos % TRACE_N] = R(ESP);
    trace_pos++;

    if (target >= 0xF1000000u && target < 0xF2000000u) {
        com_call(target);
        return;
    }
    if (target >= IMPORT_SENTINEL) {
        host_import(target);
        return;
    }
    lf2_jit_call(target);
}
