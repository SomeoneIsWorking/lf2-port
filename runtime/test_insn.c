/* Differential test: every distinct register-only instruction in the binary, run through
 * the lifter and through the real CPU with identical inputs.
 *
 * Flags already have their own test; this one checks the VALUES the lifter computes.
 * The reference is built at runtime as a small stub -- load the guest registers, execute
 * the instruction's own bytes, store them back -- so the host defines the answer.
 *
 * Three modes:
 *   (default on x86)      live differential, host execution vs lifted C
 *   --capture FILE        run the host side only and record its outputs (x86 only)
 *   --replay FILE         run the lifted C only and compare against recorded outputs;
 *                         this is the ONLY meaningful mode on a non-x86 host, and it is
 *                         selected automatically there when vectors are present
 */
#include "guest_ops.h"
#include "insn_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Host execution (live, capture) runs the binary's own instruction bytes, so those two
 * modes only exist on an x86 host. Replay is pure C and runs anywhere. */
#if defined(__x86_64__) || defined(__i386__)
#define HOST_IS_X86 1
#else
#define HOST_IS_X86 0
#endif

/* There used to be a `#ifndef MAP_32BIT / #define MAP_32BIT 0` here, on the reasoning that
 * macOS "already places mmap low enough for a 32-bit base register". That reasoning was
 * never measured, and defining the macro away would have made the portable path below
 * unreachable on the one platform it exists for. The placement is checked instead. */

/* The flag module and the generated cases expect these; the harness owns them here. */
Cpu cpu;
uint8_t *g_mem;

/* Layout is fixed: the stub addresses the FPU areas by displacement from the state
 * pointer, so these offsets are baked into its machine code below. */
typedef struct {
    uint32_t r[8];          /* 0x00 */
    uint32_t eflags;        /* 0x20 */
    uint32_t pad[7];        /* 0x24 */
    uint8_t  fpu_in[108];   /* 0x40 */
    uint8_t  pad2[4];
    uint8_t  fpu_out[108];  /* 0xB0 */
} State;

#if HOST_IS_X86
/* System V: the state pointer arrives in RDI. ESP (index 4) is deliberately not loaded
 * or stored -- the stub runs on the host stack. */
static const uint8_t PROLOGUE[] = {
    0x53,                               /* push rbx            */
    0x55,                               /* push rbp            */
    0x41, 0x54,                         /* push r12            */
    0x49, 0x89, 0xFC,                   /* mov  r12, rdi       */
    0x41, 0xFF, 0x74, 0x24, 0x20,       /* push [r12+0x20]     */
    0x9D,                               /* popfq               */
    0x41, 0x8B, 0x44, 0x24, 0x00,       /* mov eax, [r12+0x00] */
    0x41, 0x8B, 0x4C, 0x24, 0x04,       /* mov ecx, [r12+0x04] */
    0x41, 0x8B, 0x54, 0x24, 0x08,       /* mov edx, [r12+0x08] */
    0x41, 0x8B, 0x5C, 0x24, 0x0C,       /* mov ebx, [r12+0x0C] */
    0x41, 0x8B, 0x6C, 0x24, 0x14,       /* mov ebp, [r12+0x14] */
    0x41, 0x8B, 0x74, 0x24, 0x18,       /* mov esi, [r12+0x18] */
    0x41, 0x8B, 0x7C, 0x24, 0x1C,       /* mov edi, [r12+0x1C] */
};

/* Stack variants: park the host's own rsp in r13, point esp at the guest stack for the
 * duration of the instruction, then put it back. Writing esp zeroes the top half of rsp,
 * which is why guest memory has to live in the low 4 GB. */
static const uint8_t STACK_PRE[] = {
    0x41, 0x55,                         /* push r13            */
    0x49, 0x89, 0xE5,                   /* mov  r13, rsp       */
    0x41, 0x8B, 0x64, 0x24, 0x10,       /* mov  esp, [r12+0x10]*/
};
static const uint8_t STACK_POST[] = {
    0x41, 0x89, 0x64, 0x24, 0x10,       /* mov  [r12+0x10], esp*/
    0x4C, 0x89, 0xEC,                   /* mov  rsp, r13       */
    0x41, 0x5D,                         /* pop  r13            */
};

/* x87 variants: restore the FPU before the instruction, save it after. */
static const uint8_t FRSTOR_IN[]  = { 0x41, 0xDD, 0xA4, 0x24, 0x40, 0x00, 0x00, 0x00 };
static const uint8_t FNSAVE_OUT[] = { 0x41, 0xDD, 0xB4, 0x24, 0xB0, 0x00, 0x00, 0x00 };

static const uint8_t EPILOGUE[] = {
    0x41, 0x89, 0x44, 0x24, 0x00,       /* mov [r12+0x00], eax */
    0x41, 0x89, 0x4C, 0x24, 0x04,       /* mov [r12+0x04], ecx */
    0x41, 0x89, 0x54, 0x24, 0x08,       /* mov [r12+0x08], edx */
    0x41, 0x89, 0x5C, 0x24, 0x0C,       /* mov [r12+0x0C], ebx */
    0x41, 0x89, 0x6C, 0x24, 0x14,       /* mov [r12+0x14], ebp */
    0x41, 0x89, 0x74, 0x24, 0x18,       /* mov [r12+0x18], esi */
    0x41, 0x89, 0x7C, 0x24, 0x1C,       /* mov [r12+0x1C], edi */
    0x9C,                               /* pushfq              */
    0x58,                               /* pop rax             */
    0x41, 0x89, 0x44, 0x24, 0x20,       /* mov [r12+0x20], eax */
    0x41, 0x5C,                         /* pop r12             */
    0x5D,                               /* pop rbp             */
    0x5B,                               /* pop rbx             */
    0xC3,                               /* ret                 */
};

typedef void (*Stub)(State *);

static uint8_t *page;

static Stub build(const uint8_t *insn, unsigned len, int x87, int stack)
{
    size_t n = 0;
    memcpy(page + n, PROLOGUE, sizeof PROLOGUE); n += sizeof PROLOGUE;
    if (x87)   { memcpy(page + n, FRSTOR_IN, sizeof FRSTOR_IN);   n += sizeof FRSTOR_IN; }
    if (stack) { memcpy(page + n, STACK_PRE, sizeof STACK_PRE);   n += sizeof STACK_PRE; }
    /* Negative control: with LF2_X87_NULL the instruction is omitted, so the stub is a
     * bare FRSTOR/FNSAVE round-trip. If state does not survive that, the harness itself
     * is broken and no x87 result it reports means anything. */
    if (!(x87 && getenv("LF2_X87_NULL"))) { memcpy(page + n, insn, len); n += len; }
    if (stack) { memcpy(page + n, STACK_POST, sizeof STACK_POST); n += sizeof STACK_POST; }
    if (x87)   { memcpy(page + n, FNSAVE_OUT, sizeof FNSAVE_OUT); n += sizeof FNSAVE_OUT; }
    memcpy(page + n, EPILOGUE, sizeof EPILOGUE);
    __builtin___clear_cache((char *)page, (char *)page + 4096);
    return (Stub)page;
}
#endif /* HOST_IS_X86 */

/* Scratch sits mid-mapping so a signed displacement in either direction stays inside. */
enum { GUEST_SIZE = 16u << 20, SCRATCH = 8u << 20, SCRATCH_SPAN = 4096 };

/* ---- x87 state ----
 * FNSAVE's 32-bit layout: control word at 0, status at 4, tag at 8, then the eight
 * 80-bit registers from offset 28. Those slots are in *stack* order -- slot i is ST(i) --
 * not physical register order, so TOP does not enter the indexing. Getting this wrong
 * looks exactly like a corrupted round-trip: after a push the values appear one slot
 * further along, because old ST(i) really has become ST(i+1). scratch/x87/probe3.c is
 * the standalone check that pins the convention down.
 *
 * The control word is seeded to 53-bit precision, which is what MSVC's CRT sets. If that
 * is right, x87 arithmetic rounds to double at every step and the lifter's host `double`
 * should agree EXACTLY -- so this also settles an assumption recorded in
 * docs/isa-scope.md rather than leaving it asserted. */
enum { FPU_CW_PC53 = 0x027F };

/* Start half-full: TOP = 4 with four valid registers below it. Marking all eight valid
 * makes every push a stack OVERFLOW, and the CPU then yields an indefinite instead of
 * the loaded value -- which looks exactly like a lifter bug. */
enum { FPU_TOP = 1, FPU_LIVE = 7 };

#if HOST_IS_X86
static void fpu_state_init(uint8_t *area, const double *st)
{
    memset(area, 0, 108);
    area[0] = FPU_CW_PC53 & 0xff;
    area[1] = (FPU_CW_PC53 >> 8) & 0xff;
    const uint16_t sw = (uint16_t)(FPU_TOP << 11);   /* TOP is bits 11..13 */
    area[4] = (uint8_t)(sw & 0xff);
    area[5] = (uint8_t)(sw >> 8);
    /* The tag word is indexed by *physical* register, unlike the 80-bit slots above, so
     * it has to be derived from TOP: ST(i) lives in R[(TOP+i) & 7]. Leaving it hardcoded
     * silently marks the wrong registers empty and every operand reads as a masked
     * stack underflow. */
    uint16_t tw = 0xFFFF;                      /* 11 = empty everywhere */
    for (int i = 0; i < FPU_LIVE; i++) {
        const int phys = (FPU_TOP + i) & 7;
        tw &= (uint16_t)~(3u << (2 * phys));   /* 00 = valid */
    }
    area[8] = (uint8_t)(tw & 0xff);
    area[9] = (uint8_t)(tw >> 8);
    for (int i = 0; i < FPU_LIVE; i++) {
        long double v = (long double)st[i];
        memcpy(area + 28 + i * 10, &v, 10);
    }
}

static double fpu_state_get(const uint8_t *area, int i)
{
    long double v = 0;
    memcpy(&v, area + 28 + i * 10, 10);
    return (double)v;
}
#endif /* HOST_IS_X86 */

/* Deterministic pseudo-random inputs, so a failure is reproducible. Reseeded per CASE,
 * from the case's instruction BYTES rather than its position: the two ends of a golden-
 * vector comparison hold different corpora in different orders (Ghidra-derived here,
 * self-derived where no dump exists), and the same instruction must get the same input
 * stream on both. */
enum { RNG_SEED = 0x12345678 };
static uint32_t rng_state = RNG_SEED;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static void rng_case(const uint8_t *bytes, unsigned len)
{
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < len; i++) { h ^= bytes[i]; h *= 0x01000193u; }
    rng_state = (uint32_t)RNG_SEED ^ h;
    if (!rng_state) rng_state = 1;          /* xorshift must never hold zero */
}

/* ---- golden vectors ----
 *
 * The host-execution differential only means anything on x86; on any other host the
 * instruction semantics were previously simply UNTESTED -- the Mac port's first physics
 * bugs arrived with no instrument able to see them. So the x86 truth is captured once
 * into a vectors file (re/insn_vectors.bin, committed), and any host replays the lifted
 * C against it.
 *
 * Inputs are NOT stored: they regenerate from the per-case seeded stream above. Outputs
 * are stored per round -- expected registers in GUEST space, an FNV-1a hash of the
 * scratch span, and the seven live x87 slots as double bit patterns. */
enum { VEC_MAGIC = 0x5632464Cu /* "LF2V" */, VEC_VERSION = 1, VEC_MAXLEN = 16 };

typedef struct {
    uint8_t len, is_x87, pad[2];
    uint8_t bytes[VEC_MAXLEN];
} VecKey;

typedef struct {
    uint32_t regs[8];        /* guest-space expected; 0 where the comparison skips */
    uint64_t memhash;
    uint64_t st[7];          /* double bit patterns, ST(0)..ST(6); zero for non-x87 */
} VecRound;

static uint64_t fnv64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

static VecKey  *vkeys;
static VecRound *vrounds;                    /* [i * vec_rounds + r] */
static uint32_t nvec, vec_rounds;

static int vectors_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "vectors: cannot open %s\n", path); return -1; }
    uint32_t hdr[5];
    if (fread(hdr, 4, 5, f) != 5 || hdr[0] != VEC_MAGIC || hdr[1] != VEC_VERSION) {
        fprintf(stderr, "vectors: %s is not a version-%d vector file\n", path, VEC_VERSION);
        fclose(f);
        return -1;
    }
    vec_rounds = hdr[2]; nvec = hdr[3];
    if (hdr[4] != (uint32_t)RNG_SEED) {
        fprintf(stderr, "vectors: %s was captured with seed %08x, this build uses %08x;\n"
                        "         its inputs would not be the ones replayed\n",
                path, hdr[4], (uint32_t)RNG_SEED);
        fclose(f);
        return -1;
    }
    if (!vec_rounds || !nvec) {
        fprintf(stderr, "vectors: %s declares %u cases x %u rounds; it can verify "
                        "nothing\n", path, nvec, vec_rounds);
        fclose(f);
        return -1;
    }
    vkeys = calloc(nvec, sizeof *vkeys);
    vrounds = calloc((size_t)nvec * vec_rounds, sizeof *vrounds);
    if (!vkeys || !vrounds) { fclose(f); return -1; }
    for (uint32_t i = 0; i < nvec; i++) {
        if (fread(&vkeys[i], sizeof vkeys[i], 1, f) != 1 ||
            fread(&vrounds[(size_t)i * vec_rounds], sizeof *vrounds, vec_rounds, f)
                != vec_rounds) {
            fprintf(stderr, "vectors: %s truncated at case %u of %u\n", path, i, nvec);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static const VecRound *vector_of(const InsnCase *k)
{
    for (uint32_t i = 0; i < nvec; i++)
        if (vkeys[i].len == k->len && memcmp(vkeys[i].bytes, k->bytes, k->len) == 0)
            return &vrounds[(size_t)i * vec_rounds];
    return NULL;
}

enum { MODE_LIVE, MODE_CAPTURE, MODE_REPLAY };
enum { ROUNDS = 8 };

/* Guest-space expected register values, from the host result. Encodes the SAME skip and
 * adjustment rules the live comparison applies, so live and replay verify identical
 * things. Skipped slots are recorded as zero and never compared. */
static int reg_skipped(const InsnCase *k, int i)
{
    if (i == 4 && !k->is_stack) return 1;
    if (k->uses_memory && i == k->base_reg) return 1;
    return 0;
}
static uint32_t reg_adjust(const InsnCase *k, int i, uint32_t mem_base)
{
    uint32_t adj = 0;
    if (i == k->addr_reg) adj += mem_base;
    if (k->is_string && (i == 6 || i == 7)) adj += mem_base;
    if (k->is_stack && i == 4) adj += mem_base;
    return adj;
}

static int run_cases(int mode, FILE *cap, uint32_t mem_base)
{
    long checked = 0, failed = 0, reported = 0, skipped_x87 = 0, novec = 0;
    uint32_t cases_written = 0;
    static uint8_t before[SCRATCH_SPAN], after_host[SCRATCH_SPAN];
    (void)before; (void)after_host;

    /* x87 runs by default; LF2_INSN_X87=0 skips it in live and replay. Capture ignores
     * the switch -- a vectors file with holes in it would quietly weaken every replay. */
    const char *x87_env = getenv("LF2_INSN_X87");
    const int want_x87 = mode == MODE_CAPTURE || !x87_env || strcmp(x87_env, "0") != 0;

    for (int c = 0; c < insn_ncases; c++) {
        const InsnCase *k = &insn_cases[c];
        if (k->is_x87 && !want_x87) { skipped_x87++; continue; }
        if (getenv("LF2_INSN_VERBOSE")) {
            fprintf(stderr, "case %d %s ", c, k->mnemonic);
            for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }

        const VecRound *vec = NULL;
        if (mode == MODE_REPLAY) {
            vec = vector_of(k);
            if (!vec) { novec++; continue; }
        }

#if HOST_IS_X86
        Stub stub = (mode != MODE_REPLAY)
                  ? build(k->bytes, k->len, k->is_x87, k->is_stack) : NULL;
#endif
        VecKey key;
        VecRound rec[ROUNDS];
        if (mode == MODE_CAPTURE) {
            memset(&key, 0, sizeof key);
            memset(rec, 0, sizeof rec);
            key.len = (uint8_t)k->len;
            key.is_x87 = (uint8_t)k->is_x87;
            memcpy(key.bytes, k->bytes, k->len);
        }

        rng_case(k->bytes, k->len);
        for (int r = 0; r < ROUNDS; r++) {
            State want;
            for (int i = 0; i < 8; i++) want.r[i] = rnd();
            want.r[4] = 0;                              /* ESP unused */
            /* The incoming carry has to vary. With CF pinned to 0, SBB r,r is 0 whether
             * or not the borrow is honoured, so a lifter that emits ADC/SBB as plain
             * ADD/SUB passes every round -- which is exactly what happened, and it cost a
             * real bug in the shipped port (see docs/codemap.md). DF stays 0: it is a
             * direction control, not an input to arithmetic, and the string cases assume
             * forward. */
            want.eflags = 0x202u | (rnd() & 1u);        /* vary CF */

            uint32_t index_val = 0;
            if (k->is_stack) want.r[4] = SCRATCH + 1024;   /* guest stack, grows down */
            if (k->is_string) {
                /* Source and destination are placed apart so a REP MOVS does not overlap
                 * itself, and both stay inside the filled scratch span. ECX is kept small
                 * so a repeated op cannot run off the end. */
                want.r[6] = SCRATCH - 512;          /* ESI */
                want.r[7] = SCRATCH + 512;          /* EDI */
                want.r[1] = 1 + rnd() % 8;          /* ECX */
            }
            if (k->uses_memory) {
                /* Solve the base so the effective address lands on the scratch area
                 * whatever displacement the encoding carries -- displacements here run to
                 * tens of megabytes, far outside any fixed window. */
                if (k->index_reg >= 0) index_val = rnd() % 16;
                want.r[k->base_reg] =
                    (uint32_t)(SCRATCH - (uint32_t)k->disp - index_val * (uint32_t)k->scale);
                if (k->index_reg >= 0 && k->index_reg != k->base_reg)
                    want.r[k->index_reg] = index_val;
            }

            /* Same starting bytes for both runs. */
            for (uint32_t i = 0; i < SCRATCH_SPAN; i += 4)
                ST32(SCRATCH - SCRATCH_SPAN / 2 + i, rnd());

            double st_in[8];
            for (int i = 0; i < 8; i++)
                st_in[i] = (double)(int32_t)rnd() / 65536.0;

            /* ---- host side (live and capture) ---- */
#if HOST_IS_X86
            State got;
            if (mode != MODE_REPLAY) {
                memcpy(before, g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN);
                if (k->is_x87) fpu_state_init(want.fpu_in, st_in);
                got = want;
                if (k->uses_memory) got.r[k->base_reg] = mem_base + want.r[k->base_reg];
                if (k->is_string) {
                    got.r[6] = mem_base + want.r[6];
                    got.r[7] = mem_base + want.r[7];
                }
                if (k->is_stack) got.r[4] = mem_base + want.r[4];
                stub(&got);
                memcpy(after_host, g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN);
                memcpy(g_mem + SCRATCH - SCRATCH_SPAN / 2, before, SCRATCH_SPAN);
            }
            if (mode == MODE_CAPTURE) {
                for (int i = 0; i < 8; i++)
                    rec[r].regs[i] = reg_skipped(k, i)
                                   ? 0 : got.r[i] - reg_adjust(k, i, mem_base);
                rec[r].memhash = fnv64(after_host, SCRATCH_SPAN);
                if (k->is_x87)
                    for (int i = 0; i < FPU_LIVE; i++) {
                        const double v = fpu_state_get(got.fpu_out, i);
                        memcpy(&rec[r].st[i], &v, sizeof v);
                    }
                continue;                       /* nothing to compare in capture */
            }
#endif

            /* ---- lifted side (live and replay) ---- */
            memset(&cpu, 0, sizeof cpu);
            for (int i = 0; i < 8; i++) cpu.r[i] = want.r[i];
            /* Both sides must start from the same flag state, or SETcc and ADC/SBB
             * disagree before the instruction under test has done anything. */
            flags_unpack(want.eflags);
            if (k->is_x87) {
                cpu.st_top = FPU_TOP;
                for (int i = 0; i < FPU_LIVE; i++) cpu.st[(FPU_TOP + i) & 7] = st_in[i];
            }
            k->lifted();

            if (k->is_x87) {
                int bad = -1;
                double host_v = 0, mine_v = 0;
                /* Only the live part of the stack is meaningful; slots beyond it hold
                 * whatever was there before. */
                for (int i = 0; i < FPU_LIVE; i++) {
                    mine_v = FST(i);
                    if (mode == MODE_REPLAY) memcpy(&host_v, &vec[r].st[i], sizeof host_v);
#if HOST_IS_X86
                    else host_v = fpu_state_get(got.fpu_out, i);
#endif
                    if (memcmp(&host_v, &mine_v, sizeof host_v) != 0) { bad = i; break; }
                }
                if (bad >= 0) {
                    failed++;
                    if (reported < 15) {
                        fprintf(stderr, "%-10s bytes=", k->mnemonic);
                        for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                        fprintf(stderr, "  st(%d): cpu=%.17g host=%.17g\n", bad, mine_v, host_v);
                        reported++;
                    }
                    continue;
                }
            }

            checked++;
            int mem_bad;
            if (mode == MODE_REPLAY)
                mem_bad = fnv64(g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN)
                          != vec[r].memhash;
#if HOST_IS_X86
            else
                mem_bad = memcmp(after_host, g_mem + SCRATCH - SCRATCH_SPAN / 2,
                                 SCRATCH_SPAN) != 0;
#else
            else mem_bad = 0;                    /* unreachable: no host side here */
#endif
            if (mem_bad) {
                failed++;
                if (reported < 15) {
                    fprintf(stderr, "%-10s bytes=", k->mnemonic);
                    for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                    fprintf(stderr, "  memory differs\n");
                    reported++;
                }
                continue;
            }
            for (int i = 0; i < 8; i++) {
                if (reg_skipped(k, i)) continue;
                const uint32_t mine = cpu.r[i];
                uint32_t host;
                if (mode == MODE_REPLAY) host = vec[r].regs[i];
#if HOST_IS_X86
                else host = got.r[i] - reg_adjust(k, i, mem_base);
#else
                else host = mine;                /* unreachable: no host side here */
#endif
                if (mine != host) {
                    failed++;
                    if (reported < 15) {
                        fprintf(stderr, "%-10s bytes=", k->mnemonic);
                        for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                        fprintf(stderr, "  reg%d: cpu=%08x host=%08x\n", i, mine, host);
                        reported++;
                    }
                    break;
                }
            }
        }

        if (mode == MODE_CAPTURE) {
            fwrite(&key, sizeof key, 1, cap);
            fwrite(rec, sizeof rec[0], ROUNDS, cap);
            cases_written++;
        }
    }

    if (mode == MODE_CAPTURE) {
        printf("%u cases x %d rounds captured\n", cases_written, ROUNDS);
        return (int)cases_written;               /* caller patches the header */
    }
    printf("\n%d cases x %d rounds = %ld checks, %ld mismatches (%ld x87 skipped%s",
           insn_ncases, ROUNDS, checked, failed, skipped_x87,
           mode == MODE_REPLAY ? "" : ")\n");
    if (mode == MODE_REPLAY) {
        printf(", %ld without a vector)\n", novec);
        /* A replay that matched nothing is a broken instrument, not a pass: it happens
         * when the corpus and the vectors drifted apart entirely. */
        if (!checked) {
            fprintf(stderr, "replay compared NOTHING: no corpus case has a vector\n");
            return 2;
        }
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *capture_path = NULL, *replay_path = NULL;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--capture") == 0 && a + 1 < argc) capture_path = argv[++a];
        else if (strcmp(argv[a], "--replay") == 0 && a + 1 < argc) replay_path = argv[++a];
        else { fprintf(stderr, "usage: %s [--capture FILE | --replay FILE]\n", argv[0]); return 2; }
    }

#if !HOST_IS_X86
    if (capture_path) {
        fprintf(stderr, "capture executes the instruction bytes on the host; it needs x86\n");
        return 2;
    }
    /* On a non-x86 host the only meaningful run is a golden-vector replay. */
    if (!replay_path) replay_path = getenv("LF2_INSN_VECTORS");
#ifdef LF2_VECTORS_PATH
    if (!replay_path) replay_path = LF2_VECTORS_PATH;
#endif
    if (!replay_path) {
        printf("skipped: no x86 host and no golden vectors named; nothing was compared\n");
        return 77;
    }
#endif
    if (capture_path && getenv("LF2_X87_NULL")) {
        fprintf(stderr, "refusing to capture with LF2_X87_NULL set: the stub would omit\n"
                        "every x87 instruction and the vectors would record the harness\n");
        return 2;
    }

    const int mode = capture_path ? MODE_CAPTURE : replay_path ? MODE_REPLAY : MODE_LIVE;

    if (mode == MODE_REPLAY) {
        if (vectors_load(replay_path) != 0) {
#if HOST_IS_X86
            return 2;
#else
            printf("skipped: golden vectors unusable (%s); nothing was compared.\n"
                   "         Regenerate on an x86 machine: test_insn --capture "
                   "re/insn_vectors.bin\n", replay_path);
            return 77;
#endif
        }
        if (vec_rounds != ROUNDS) {
            fprintf(stderr, "vectors: %s holds %u rounds per case, this build runs %d\n",
                    replay_path, vec_rounds, ROUNDS);
            return 2;
        }
        /* No host execution, so guest memory can live anywhere the OS likes. */
        g_mem = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_mem == MAP_FAILED) { perror("mmap guest"); return 2; }
        return run_cases(MODE_REPLAY, NULL, 0);
    }

#if HOST_IS_X86
    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) { perror("mmap"); return 2; }

    /* Guest memory must sit in the low 4 GB for the memory cases: the host executes the
     * instruction with a 32-bit base register, so it needs g_mem + guest_addr to fit in
     * one. Both sides then address the identical bytes.
     *
     * MAP_32BIT guarantees that but is a Linux extension; macOS has no equivalent, so
     * there the placement is requested by hint and CHECKED. A hint is advisory -- the
     * kernel may put the mapping anywhere -- so a mapping that lands high is unmapped and
     * the next hint tried, rather than trusted. 16 MB fits in the low 4 GB many times
     * over; if none of the hints land, that is reported and the run skips, because
     * comparing against a base the host cannot address would not be a comparison. */
/* -DLF2_NO_MAP_32BIT forces the portable path on Linux, so it can actually be run
     * here rather than only compiled somewhere nobody has. */
#if defined(MAP_32BIT) && !defined(LF2_NO_MAP_32BIT)
    g_mem = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
#else
    g_mem = MAP_FAILED;
    for (uintptr_t hint = 0x10000000u; hint < 0xf0000000u; hint += 0x10000000u) {
        void *p = mmap((void *)hint, GUEST_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) continue;
        if ((uintptr_t)p + GUEST_SIZE <= 0x100000000u) { g_mem = p; break; }
        munmap(p, GUEST_SIZE);
    }
    if (g_mem == MAP_FAILED) {
        printf("SKIP: could not place guest memory in the low 4 GB, so the host cannot\n"
               "      address it with a 32-bit base. No instructions were compared.\n");
        return 77;
    }
#endif
    if (g_mem == MAP_FAILED) { perror("mmap guest"); return 2; }
    if ((uintptr_t)g_mem + GUEST_SIZE > 0x100000000u) {
        printf("SKIP: guest memory landed at %p, above the 4 GB the host can address with\n"
               "      a 32-bit base. No instructions were compared.\n", (void *)g_mem);
        return 77;
    }
    const uint32_t mem_base = (uint32_t)(uintptr_t)g_mem;

    if (mode == MODE_CAPTURE) {
        FILE *cap = fopen(capture_path, "wb");
        if (!cap) { perror(capture_path); return 2; }
        uint32_t hdr[5] = { VEC_MAGIC, VEC_VERSION, ROUNDS, 0, (uint32_t)RNG_SEED };
        fwrite(hdr, 4, 5, cap);
        const int written = run_cases(MODE_CAPTURE, cap, mem_base);
        if (written <= 0) {
            fprintf(stderr, "capture produced no cases; not writing a header for it\n");
            fclose(cap);
            remove(capture_path);
            return 2;
        }
        hdr[3] = (uint32_t)written;
        fseek(cap, 0, SEEK_SET);
        fwrite(hdr, 4, 5, cap);
        fclose(cap);
        return 0;
    }
    return run_cases(MODE_LIVE, NULL, mem_base);
#else
    /* Unreachable: the non-x86 path above always replays or skips. */
    return 77;
#endif
}
