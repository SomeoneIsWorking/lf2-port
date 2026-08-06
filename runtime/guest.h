/* Guest CPU state and memory for the recompiled code. */
#ifndef GUEST_H
#define GUEST_H

#include <stdint.h>
#include <stdlib.h>

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

/* Flags are evaluated lazily: arithmetic records its operands and result, and the
 * individual flags are derived only when a Jcc/SETcc actually reads one. Only 5 sites
 * in the whole binary (PUSHFD/POPFD) ever need the full register materialised. */
/* F_ADC and F_SBB are distinct from F_ADD/F_SUB because the carry-in shifts the
 * boundary case: with a borrow in, SBB sets CF when a == b, which SUB does not. Folding
 * them together makes the carry-materialising idiom (NEG/SBB reg,reg) collapse to zero. */
enum { F_ADD, F_SUB, F_LOGIC, F_INC, F_DEC, F_SHL, F_SHR, F_SAR, F_MUL, F_ADC, F_SBB,
       F_NONE };

typedef struct {
    uint32_t r[8];
    uint32_t eip;
    uint8_t  op;        /* last flag-setting operation kind */
    uint8_t  size;      /* 1, 2 or 4 bytes */
    uint32_t a, b, res; /* its operands and result */
    uint32_t cin;       /* carry into the last ADC/SBB; unused by other kinds */
    uint32_t cf_hint;   /* carry for shifts/mul, where res cannot recover it */
    double   st[8];     /* x87 stack; host double -- see docs/isa-scope.md */
    int      st_top;
    uint16_t fsw;
    /* x87 control word. The game reads it back (FNSTCW) to check that exceptions are
     * masked, so it has to hold a plausible value; 0x027F is MSVC's default -- all
     * exceptions masked, round-to-nearest, 53-bit precision. Precision control is not
     * acted on: x87 is evaluated in host double throughout, which is 53-bit anyway.
     * See docs/isa-scope.md. */
    uint16_t fcw;
} Cpu;

extern Cpu cpu;
extern uint8_t *g_mem;      /* guest address N lives at g_mem[N] */
extern uint32_t g_image_lo, g_image_hi;   /* mapped extent of the loaded PE image */

#define R(i)  (cpu.r[(i)])

/* getenv is a linear scan of the environment. Debug flags read on paths the game hits
 * millions of times per load -- fscanf alone is 2.5M calls -- must be resolved once.
 * Usage: static int c = -1; if (env_flag("LF2_X", &c)) ... */
static inline int env_flag(const char *name, int *cache)
{
    if (*cache < 0) *cache = (getenv(name) != NULL);
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
void rwatch_raw_flush(const char *when);   /* LF2_READ_WATCH_RAW: per-dword read profile */
void rwatch_init(void);
void dump_fn_trace(void);
int  rwatch_triggered(void);
#define RWATCH(a) \
    do { if (__builtin_expect((a) - g_rwatch_lo < g_rwatch_hi - g_rwatch_lo, 0)) \
             rwatch_hit(a); } while (0)

static inline uint8_t  LD8 (uint32_t a) { RWATCH(a); return *(uint8_t  *)(g_mem + a); }
static inline uint16_t LD16(uint32_t a) { return *(uint16_t *)(g_mem + a); }
static inline uint32_t LD32(uint32_t a) { RWATCH(a); return *(uint32_t *)(g_mem + a); }
static inline void ST8 (uint32_t a, uint8_t  v) { *(uint8_t  *)(g_mem + a) = v; }
static inline void ST16(uint32_t a, uint16_t v) { *(uint16_t *)(g_mem + a) = v; }
static inline void ST32(uint32_t a, uint32_t v) { *(uint32_t *)(g_mem + a) = v; }

static inline void PUSH32(uint32_t v) { R(ESP) -= 4; ST32(R(ESP), v); }
static inline uint32_t POP32(void)    { uint32_t v = LD32(R(ESP)); R(ESP) += 4; return v; }

/* Record a flag-setting result. */
static inline void FLAGS(uint8_t op, uint8_t size, uint32_t a, uint32_t b, uint32_t res)
{
    cpu.op = op; cpu.size = size; cpu.a = a; cpu.b = b; cpu.res = res; cpu.cin = 0;
}

/* ADC/SBB need the carry that went in, not just the operands. */
static inline void FLAGS_C(uint8_t op, uint8_t size, uint32_t a, uint32_t b,
                           uint32_t res, uint32_t cin)
{
    cpu.op = op; cpu.size = size; cpu.a = a; cpu.b = b; cpu.res = res; cpu.cin = cin;
}

int  flag_zf(void);
int  flag_sf(void);
int  flag_cf(void);
int  flag_of(void);
int  flag_pf(void);
uint32_t flags_pack(void);      /* PUSHFD */
void     flags_unpack(uint32_t);/* POPFD  */

typedef struct { uint32_t addr; void (*fn)(void); } GuestFunc;
extern const GuestFunc g_funcs[];
extern const int g_nfuncs;

/* Imported functions get a sentinel address written into their IAT slot, so an
 * indirect call through the IAT lands in dispatch() with something we can name. */
enum { IMPORT_SENTINEL = 0xF0000000u };

/* Thread information block. The CRT's SEH prologues address it through FS, so FS-relative
 * accesses are rebased here instead of landing on absolute address 0. */
enum { TIB_BASE = 0x7FFDE000u };
void host_import(uint32_t sentinel);

void guest_init(void);
void guest_load_image(const char *exe_path);
void dispatch(uint32_t target);  /* indirect call/jump */

#endif /* GUEST_H */
