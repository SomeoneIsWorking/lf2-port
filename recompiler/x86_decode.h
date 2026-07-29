/* Length-accurate x86-32 instruction decoder.
 *
 * The first job of the recompiler is to split .text into instructions. Getting the
 * *length* right needs no per-opcode semantics -- only whether an opcode carries a
 * ModRM byte and what size immediate follows -- so it is the natural first milestone,
 * and it is verifiable against all 70508 instructions Ghidra found. */
#ifndef X86_DECODE_H
#define X86_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { X86_MAX_INSN_LEN = 15 };

/* Legacy prefix bits, as seen on the instruction. */
enum {
    X86_PFX_LOCK     = 1u << 0,
    X86_PFX_REPNE    = 1u << 1, /* F2 */
    X86_PFX_REP      = 1u << 2, /* F3 */
    X86_PFX_SEG      = 1u << 3, /* any of 2E 36 3E 26 64 65 */
    X86_PFX_OPSIZE   = 1u << 4, /* 66 */
    X86_PFX_ADDRSIZE = 1u << 5, /* 67 */
};

typedef struct {
    uint8_t length;      /* total bytes, prefixes included */
    uint8_t prefixes;    /* X86_PFX_* bits */
    uint8_t seg_prefix;  /* raw segment-override byte, 0 if none */
    uint8_t opcode;      /* primary opcode byte (after any 0F / 0F 38 / 0F 3A escape) */
    uint8_t map;         /* 1 = one-byte, 2 = 0F, 3 = 0F 38, 4 = 0F 3A */
    bool    has_modrm;
    uint8_t modrm;
    bool    has_sib;
    uint8_t sib;
    uint8_t disp_size;   /* 0, 1, 2 or 4 */
    uint8_t imm_size;    /* 0, 1, 2, 4 or 6 (far pointer) */
    int32_t disp;
    int64_t imm;         /* sign-extended for 1/2/4-byte immediates */
} x86_insn;

/* Decode one instruction from `bytes` (at most `len` readable).
 * Returns true and fills `out` on success; false if the bytes are not a decodable
 * instruction or would run past `len`. */
bool x86_decode(const uint8_t *bytes, size_t len, x86_insn *out);

#endif /* X86_DECODE_H */
