/* Differential test: every distinct register-only instruction in the binary, run through
 * the lifter and through the real CPU with identical inputs.
 *
 * Flags already have their own test; this one checks the VALUES the lifter computes.
 * The reference is built at runtime as a small stub -- load the guest registers, execute
 * the instruction's own bytes, store them back -- so the host defines the answer.
 */
#include "guest_ops.h"
#include "insn_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* This test executes the binary's own instruction bytes on the host, so it only means
 * anything on an x86 host -- on Apple Silicon there is nothing to compare against.
 * MAP_32BIT is also a Linux extension; macOS on x86 already places mmap low enough for a
 * 32-bit base register, so requesting it is unnecessary there. */
#if defined(__x86_64__) || defined(__i386__)
#define HOST_IS_X86 1
#else
#define HOST_IS_X86 0
#endif

#ifndef MAP_32BIT
#define MAP_32BIT 0
#endif

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

/* Scratch sits mid-mapping so a signed displacement in either direction stays inside. */
enum { GUEST_SIZE = 16u << 20, SCRATCH = 8u << 20, SCRATCH_SPAN = 4096 };

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

/* ---- x87 state ----
 * FNSAVE's 32-bit layout: control word at 0, status at 4, tag at 8, then the eight
 * physical registers as 80-bit values from offset 28. ST(i) is physical R[(TOP+i) & 7],
 * with TOP in status bits 11..13.
 *
 * The control word is seeded to 53-bit precision, which is what MSVC's CRT sets. If that
 * is right, x87 arithmetic rounds to double at every step and the lifter's host `double`
 * should agree EXACTLY -- so this also settles an assumption recorded in
 * docs/isa-scope.md rather than leaving it asserted. */
enum { FPU_CW_PC53 = 0x027F };

/* Start half-full: TOP = 4 with four valid registers below it. Marking all eight valid
 * makes every push a stack OVERFLOW, and the CPU then yields an indefinite instead of
 * the loaded value -- which looks exactly like a lifter bug. */
enum { FPU_TOP = 4, FPU_LIVE = 4 };

static void fpu_state_init(uint8_t *area, const double *st)
{
    memset(area, 0, 108);
    area[0] = FPU_CW_PC53 & 0xff;
    area[1] = (FPU_CW_PC53 >> 8) & 0xff;
    const uint16_t sw = (uint16_t)(FPU_TOP << 11);   /* TOP is bits 11..13 */
    area[4] = (uint8_t)(sw & 0xff);
    area[5] = (uint8_t)(sw >> 8);
    area[8] = 0xFF; area[9] = 0x00;           /* R0..R3 empty, R4..R7 valid */
    for (int i = 0; i < FPU_LIVE; i++) {
        long double v = (long double)st[i];
        memcpy(area + 28 + ((FPU_TOP + i) & 7) * 10, &v, 10);
    }
}

static double fpu_state_get(const uint8_t *area, int i)
{
    const unsigned sw = (unsigned)area[4] | ((unsigned)area[5] << 8);
    const int top = (int)((sw >> 11) & 7);
    long double v = 0;
    memcpy(&v, area + 28 + ((top + i) & 7) * 10, 10);
    return (double)v;
}

/* Deterministic pseudo-random inputs, so a failure is reproducible. */
static uint32_t rng_state = 0x12345678u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(void)
{
#if !HOST_IS_X86
    printf("skipped: the instruction differential needs an x86 host to compare against\n");
    return 0;
#else
    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) { perror("mmap"); return 2; }

    /* Guest memory must sit in the low 4 GB for the memory cases: the host executes the
     * instruction with a 32-bit base register, so it needs g_mem + guest_addr to fit in
     * one. MAP_32BIT guarantees that. Both sides then address the identical bytes. */
    g_mem = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (g_mem == MAP_FAILED) { perror("mmap guest"); return 2; }
    const uint32_t mem_base = (uint32_t)(uintptr_t)g_mem;

    enum { ROUNDS = 8 };
    long checked = 0, failed = 0, reported = 0;
    static uint8_t before[SCRATCH_SPAN], after_host[SCRATCH_SPAN];

    /* The x87 path is not trustworthy yet: setting up a believable FPU state through
     * FRSTOR has already produced three harness bugs of its own (all registers marked
     * valid making every push an overflow, a mis-encoded TOP field, and a writeback that
     * still yields zero). Until it is validated by a negative control like the integer
     * path, it stays off -- a test that reports failures it cannot stand behind is worse
     * than no test. Set LF2_INSN_X87=1 to work on it. */
    const int want_x87 = getenv("LF2_INSN_X87") != NULL;
    long skipped_x87 = 0;

    for (int c = 0; c < insn_ncases; c++) {
        const InsnCase *k = &insn_cases[c];
        if (k->is_x87 && !want_x87) { skipped_x87++; continue; }
        if (getenv("LF2_INSN_VERBOSE")) {
            fprintf(stderr, "case %d %s ", c, k->mnemonic);
            for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        Stub stub = build(k->bytes, k->len, k->is_x87, k->is_stack);

        for (int r = 0; r < ROUNDS; r++) {
            State want;
            for (int i = 0; i < 8; i++) want.r[i] = rnd();
            want.r[4] = 0;                              /* ESP unused */
            want.eflags = 0x202;

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
            memcpy(before, g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN);

            double st_in[8];
            for (int i = 0; i < 8; i++)
                st_in[i] = (double)(int32_t)rnd() / 65536.0;
            if (k->is_x87) fpu_state_init(want.fpu_in, st_in);
            if (k->is_x87 && getenv("LF2_X87_DUMP")) {
                static int sh;
                if (!sh++) {
                    fprintf(stderr, "in st_in[0]=%f R4:", st_in[0]);
                    for (int b = 68; b < 78; b++) fprintf(stderr, " %02x", want.fpu_in[b]);
                    fprintf(stderr, "  sizeof(long double)=%zu\n", sizeof(long double));
                }
            }

            State got = want;
            if (k->uses_memory) got.r[k->base_reg] = mem_base + want.r[k->base_reg];
            if (k->is_stack) want.r[4] = SCRATCH + 1024;   /* guest stack, grows down */
            if (k->is_string) {
                got.r[6] = mem_base + want.r[6];
                got.r[7] = mem_base + want.r[7];
            }
            if (k->is_stack) got.r[4] = mem_base + want.r[4];
            stub(&got);
            memcpy(after_host, g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN);

            memcpy(g_mem + SCRATCH - SCRATCH_SPAN / 2, before, SCRATCH_SPAN);
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

            if (k->is_x87 && getenv("LF2_X87_DUMP")) {
                static int shown;
                if (!shown++) {
                    fprintf(stderr, "fpu_out cw=%02x%02x sw=%02x%02x tw=%02x%02x R0..R2:",
                            got.fpu_out[1], got.fpu_out[0], got.fpu_out[5], got.fpu_out[4],
                            got.fpu_out[9], got.fpu_out[8]);
                    for (int b = 28; b < 58; b++) fprintf(stderr, " %02x", got.fpu_out[b]);
                    fprintf(stderr, "\n");
                }
            }
            if (k->is_x87) {
                int bad = -1;
                /* Only the live part of the stack is meaningful; slots beyond it hold
                 * whatever was there before. */
                for (int i = 0; i < FPU_LIVE; i++) {
                    const double host_v = fpu_state_get(got.fpu_out, i);
                    const double mine_v = FST(i);
                    if (memcmp(&host_v, &mine_v, sizeof host_v) != 0) { bad = i; break; }
                }
                if (bad >= 0) {
                    failed++;
                    if (reported < 15) {
                        fprintf(stderr, "%-10s bytes=", k->mnemonic);
                        for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                        fprintf(stderr, "  st(%d): cpu=%.17g host=%.17g\n",
                                bad, FST(bad), fpu_state_get(got.fpu_out, bad));
                        reported++;
                    }
                    continue;
                }
            }

            checked++;
            if (memcmp(after_host, g_mem + SCRATCH - SCRATCH_SPAN / 2, SCRATCH_SPAN) != 0) {
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
                if (i == 4 && !k->is_stack) continue;
                /* The base register holds a host address on one side by construction. */
                if (k->uses_memory && i == k->base_reg) continue;
                /* ESI/EDI hold host addresses on one side by construction, and the string
                 * ops only advance them, so the offset survives and can be compensated. */
                uint32_t mine = cpu.r[i];
                if (i == k->addr_reg) mine += mem_base;
                if (k->is_string && (i == 6 || i == 7)) mine += mem_base;
                if (k->is_stack && i == 4) mine += mem_base;
                if (mine != got.r[i]) {
                    failed++;
                    if (reported < 15) {
                        fprintf(stderr, "%-10s bytes=", k->mnemonic);
                        for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                        fprintf(stderr, "  reg%d: cpu=%08x host=%08x\n", i, mine, got.r[i]);
                        reported++;
                    }
                    break;
                }
            }
        }
    }

    printf("\n%d cases x %d rounds = %ld checks, %ld mismatches"
           " (%ld x87 cases skipped -- harness not yet validated)\n",
           insn_ncases, ROUNDS, checked, failed, skipped_x87);
    return failed ? 1 : 0;
#endif
}
