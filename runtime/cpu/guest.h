/* LF2 guest memory and the temporary state view used by native title code.
 * runtime/cpu/jit_executor.c will bind this view to shared/x86port's canonical CPU. */
#ifndef GUEST_H
#define GUEST_H

#include "environment.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

typedef struct {
    uint32_t r[8];
    uint32_t eip;
} Cpu;

extern Cpu cpu;
extern uint8_t *g_mem;                  /* guest address N lives at g_mem[N] */
extern uint32_t g_image_lo, g_image_hi; /* mapped extent of the loaded PE image */

#define R(i) (cpu.r[(i)])

/* getenv is a linear scan of the environment. Debug flags read on paths the game hits
 * millions of times per load -- fscanf alone is 2.5M calls -- must be resolved once.
 * Usage: static int c = -1; if (env_flag("LF2_X", &c)) ... */
static inline int env_flag(Lf2EnvironmentKey key, int *cache)
{
    if (*cache < 0) *cache = lf2_environment_enabled(key);
    return *cache;
}

/* Read-watch over [lo, hi). LF2 reads its key state out of its own array rather than
 * asking Windows, so following input means watching loads, not imports. The test is one
 * subtract and one compare, and with lo == hi == 0 the span is zero so it is always false
 * -- disabled costs a predictable not-taken branch. */
extern uint32_t g_rwatch_lo, g_rwatch_hi;
void rwatch_hit(uint32_t a);
void rwatch_frame(void);
void rwatch_selftest(void);
void rwatch_raw_flush(const char *when); /* LF2_READ_WATCH_RAW: per-dword read profile */
void rwatch_init(void);
int rwatch_triggered(void);
#define RWATCH(a)                                                                                                      \
    do {                                                                                                               \
        if (__builtin_expect((a) - g_rwatch_lo < g_rwatch_hi - g_rwatch_lo, 0)) rwatch_hit(a);                         \
    } while (0)

static inline uint8_t LD8(uint32_t a)
{
    RWATCH(a);
    return *(uint8_t *)(g_mem + a);
}
static inline uint16_t LD16(uint32_t a)
{
    return *(uint16_t *)(g_mem + a);
}
static inline uint32_t LD32(uint32_t a)
{
    RWATCH(a);
    return *(uint32_t *)(g_mem + a);
}
static inline void ST8(uint32_t a, uint8_t v)
{
    *(uint8_t *)(g_mem + a) = v;
}
static inline void ST16(uint32_t a, uint16_t v)
{
    *(uint16_t *)(g_mem + a) = v;
}
static inline void ST32(uint32_t a, uint32_t v)
{
    *(uint32_t *)(g_mem + a) = v;
}

/* Native overrides access guest doubles as data. These helpers do not implement x87;
 * architectural floating-point state and semantics belong exclusively to x86port. */
static inline double guest_load_f64(uint32_t address)
{
    double value;
    uint8_t *bytes = (uint8_t *)&value;
    for (size_t index = 0; index < sizeof value; ++index) bytes[index] = g_mem[address + index];
    return value;
}

static inline void guest_store_f64(uint32_t address, double value)
{
    const uint8_t *bytes = (const uint8_t *)&value;
    for (size_t index = 0; index < sizeof value; ++index) g_mem[address + index] = bytes[index];
}

static inline void PUSH32(uint32_t v)
{
    R(ESP) -= 4;
    ST32(R(ESP), v);
}
static inline uint32_t POP32(void)
{
    uint32_t v = LD32(R(ESP));
    R(ESP) += 4;
    return v;
}

/* Imported functions get a sentinel address written into their IAT slot, so an
 * indirect call through the IAT reaches the executor interception boundary with
 * something we can name. */
enum { IMPORT_SENTINEL = 0xF0000000u };

/* Thread information block. The CRT's SEH prologues address it through FS, so FS-relative
 * accesses are rebased here instead of landing on absolute address 0. */
enum { TIB_BASE = 0x7FFDE000u };
void host_import(uint32_t sentinel);

void guest_init(void);
void guest_load_image(const char *exe_path);
void dispatch(uint32_t target); /* temporary native-call adapter; routes guest code to JIT */

#endif /* GUEST_H */
