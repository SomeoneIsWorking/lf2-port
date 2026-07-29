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

/* The flag module and the generated cases expect these; the harness owns them here. */
Cpu cpu;
uint8_t *g_mem;

typedef struct { uint32_t r[8]; uint32_t eflags; } State;

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

static Stub build(const uint8_t *insn, unsigned len)
{
    size_t n = 0;
    memcpy(page + n, PROLOGUE, sizeof PROLOGUE); n += sizeof PROLOGUE;
    memcpy(page + n, insn, len);                 n += len;
    memcpy(page + n, EPILOGUE, sizeof EPILOGUE);
    __builtin___clear_cache((char *)page, (char *)page + 4096);
    return (Stub)page;
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
    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) { perror("mmap"); return 2; }

    /* Register-only forms should never touch memory, but map the whole space anyway so a
     * stray access reports a mismatch instead of killing the harness. */
    g_mem = mmap(NULL, 0x100000000ull, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_mem == MAP_FAILED) { perror("mmap guest"); return 2; }

    enum { ROUNDS = 8 };
    long checked = 0, failed = 0, reported = 0;

    for (int c = 0; c < insn_ncases; c++) {
        const InsnCase *k = &insn_cases[c];
        if (getenv("LF2_INSN_VERBOSE")) {
            fprintf(stderr, "case %d %s ", c, k->mnemonic);
            for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        Stub stub = build(k->bytes, k->len);

        for (int r = 0; r < ROUNDS; r++) {
            State want;
            for (int i = 0; i < 8; i++) want.r[i] = rnd();
            want.r[4] = 0;                              /* ESP unused */
            want.eflags = 0x202;

            memset(&cpu, 0, sizeof cpu);
            for (int i = 0; i < 8; i++) cpu.r[i] = want.r[i];
            /* Both sides must start from the same flag state, or SETcc and ADC/SBB
             * disagree before the instruction under test has done anything. */
            flags_unpack(want.eflags);

            State got = want;
            stub(&got);
            k->lifted();

            checked++;
            for (int i = 0; i < 8; i++) {
                if (i == 4) continue;
                if (cpu.r[i] != got.r[i]) {
                    failed++;
                    if (reported < 15) {
                        fprintf(stderr, "%-10s bytes=", k->mnemonic);
                        for (unsigned b = 0; b < k->len; b++) fprintf(stderr, "%02x", k->bytes[b]);
                        fprintf(stderr, "  reg%d: cpu=%08x host=%08x\n", i, cpu.r[i], got.r[i]);
                        reported++;
                    }
                    break;
                }
            }
        }
    }

    printf("\n%d cases x %d rounds = %ld checks, %ld mismatches\n",
           insn_ncases, ROUNDS, checked, failed);
    return failed ? 1 : 0;
}
