#include "x86_decode.h"

#include <string.h>

/* Immediate size classes. Z/V depend on the operand-size prefix. */
enum { I_NONE = 0, I_B, I_W, I_D, I_Z, I_P, I_MOFFS, I_ENTER, I_GRP3 };

#define M 0x80u             /* opcode carries a ModRM byte */
#define OP(imm, modrm) ((uint8_t)((imm) | (modrm)))

/* One-byte opcode map: low bits = immediate class, M bit = has ModRM. */
static const uint8_t onebyte[256] = {
    /* 00-07 ADD */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 08-0F OR  */      M, M, M, M, I_B, I_Z, 0, 0,      /* 0F is the escape, handled early */
    /* 10-17 ADC */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 18-1F SBB */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 20-27 AND */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 28-2F SUB */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 30-37 XOR */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 38-3F CMP */      M, M, M, M, I_B, I_Z, 0, 0,
    /* 40-47 INC */      0, 0, 0, 0, 0, 0, 0, 0,
    /* 48-4F DEC */      0, 0, 0, 0, 0, 0, 0, 0,
    /* 50-57 PUSH */     0, 0, 0, 0, 0, 0, 0, 0,
    /* 58-5F POP  */     0, 0, 0, 0, 0, 0, 0, 0,
    /* 60-67 */          0, 0, M, M, 0, 0, 0, 0,          /* PUSHA POPA BOUND ARPL, then prefixes */
    /* 68-6F */          I_Z, OP(I_Z, M), I_B, OP(I_B, M), 0, 0, 0, 0,  /* PUSH IMUL PUSH IMUL INS OUTS */
    /* 70-77 Jcc */      I_B, I_B, I_B, I_B, I_B, I_B, I_B, I_B,
    /* 78-7F Jcc */      I_B, I_B, I_B, I_B, I_B, I_B, I_B, I_B,
    /* 80-83 grp1 */     OP(I_B, M), OP(I_Z, M), OP(I_B, M), OP(I_B, M),
    /* 84-87 TEST XCHG */ M, M, M, M,
    /* 88-8F MOV LEA POP */ M, M, M, M, M, M, M, M,
    /* 90-97 XCHG/NOP */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 98-9F */          0, 0, I_P, 0, 0, 0, 0, 0,        /* CWDE CDQ CALLF WAIT PUSHFD POPFD SAHF LAHF */
    /* A0-A3 MOV moffs */ I_MOFFS, I_MOFFS, I_MOFFS, I_MOFFS,
    /* A4-A7 MOVS CMPS */ 0, 0, 0, 0,
    /* A8-A9 TEST */     I_B, I_Z,
    /* AA-AF STOS LODS SCAS */ 0, 0, 0, 0, 0, 0,
    /* B0-B7 MOV r8,Ib */ I_B, I_B, I_B, I_B, I_B, I_B, I_B, I_B,
    /* B8-BF MOV r32,Iv */ I_Z, I_Z, I_Z, I_Z, I_Z, I_Z, I_Z, I_Z,
    /* C0-C1 shift grp */ OP(I_B, M), OP(I_B, M),
    /* C2-C3 RET */      I_W, 0,
    /* C4-C5 LES LDS */  M, M,
    /* C6-C7 MOV imm */  OP(I_B, M), OP(I_Z, M),
    /* C8-C9 ENTER LEAVE */ I_ENTER, 0,
    /* CA-CF RETF INT */ I_W, 0, 0, I_B, 0, 0,
    /* D0-D3 shift grp */ M, M, M, M,
    /* D4-D7 AAM AAD SALC XLAT */ I_B, I_B, 0, 0,
    /* D8-DF x87 */      M, M, M, M, M, M, M, M,
    /* E0-E3 LOOP JCXZ */ I_B, I_B, I_B, I_B,
    /* E4-E7 IN OUT */   I_B, I_B, I_B, I_B,
    /* E8-EB CALL JMP */ I_Z, I_Z, I_P, I_B,
    /* EC-EF IN OUT DX */ 0, 0, 0, 0,
    /* F0-F3 prefixes/INT1 */ 0, 0, 0, 0,
    /* F4-F5 HLT CMC */  0, 0,
    /* F6-F7 grp3 */     OP(I_GRP3, M), OP(I_GRP3, M),
    /* F8-FD flags */    0, 0, 0, 0, 0, 0,
    /* FE-FF grp4/5 */   M, M,
};

/* Two-byte (0F xx) map. Most SSE/MMX opcodes take a ModRM and no immediate, so that is
 * the default and only the exceptions are listed. */
static uint8_t twobyte_entry(uint8_t op)
{
    switch (op) {
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0B:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
    case 0xA0: case 0xA1: case 0xA2: case 0xA8: case 0xA9: case 0xAA:
    case 0x0E: case 0xFF:
        return I_NONE;                       /* no ModRM, no immediate */
    case 0xC8: case 0xC9: case 0xCA: case 0xCB:
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        return I_NONE;                       /* BSWAP r32 */
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        return I_Z;                          /* Jcc rel16/32, no ModRM */
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0xA4: case 0xAC: case 0xBA:
    case 0xC2: case 0xC4: case 0xC5: case 0xC6:
        return OP(I_B, M);                   /* ModRM + ib */
    default:
        return M;                            /* ModRM, no immediate */
    }
}

static uint8_t imm_bytes(uint8_t cls, bool opsize16, bool addrsize16, uint8_t modrm_reg,
                         uint8_t opcode)
{
    switch (cls) {
    case I_B:     return 1;
    case I_W:     return 2;
    case I_D:     return 4;
    case I_Z:     return opsize16 ? 2 : 4;
    case I_P:     return opsize16 ? 4 : 6;   /* far ptr: offset + 2-byte selector */
    case I_MOFFS: return addrsize16 ? 2 : 4;
    case I_ENTER: return 3;                  /* iw + ib */
    case I_GRP3:
        /* F6 /0,/1 = TEST Eb,Ib and F7 /0,/1 = TEST Ev,Iz carry an immediate;
         * NOT/NEG/MUL/IMUL/DIV/IDIV in the same group do not. */
        if (modrm_reg > 1) return 0;
        return (opcode == 0xF6) ? 1 : (opsize16 ? 2 : 4);
    default:      return 0;
    }
}

bool x86_decode(const uint8_t *bytes, size_t len, x86_insn *out)
{
    size_t p = 0;
    x86_insn in;
    memset(&in, 0, sizeof in);

    /* --- legacy prefixes --- */
    for (;;) {
        if (p >= len) return false;
        uint8_t b = bytes[p];
        if (b == 0xF0)                       in.prefixes |= X86_PFX_LOCK;
        else if (b == 0xF2)                  in.prefixes |= X86_PFX_REPNE;
        else if (b == 0xF3)                  in.prefixes |= X86_PFX_REP;
        else if (b == 0x66)                  in.prefixes |= X86_PFX_OPSIZE;
        else if (b == 0x67)                  in.prefixes |= X86_PFX_ADDRSIZE;
        else if (b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            in.prefixes |= X86_PFX_SEG;
            in.seg_prefix = b;
        } else break;
        p++;
    }

    const bool opsize16   = (in.prefixes & X86_PFX_OPSIZE) != 0;
    const bool addrsize16 = (in.prefixes & X86_PFX_ADDRSIZE) != 0;

    /* --- opcode, following 0F / 0F 38 / 0F 3A escapes --- */
    if (p >= len) return false;
    uint8_t entry;
    if (bytes[p] == 0x0F) {
        p++;
        if (p >= len) return false;
        if (bytes[p] == 0x38 || bytes[p] == 0x3A) {
            bool has_ib = (bytes[p] == 0x3A);
            in.map = (uint8_t)(bytes[p] == 0x38 ? 3 : 4);
            p++;
            if (p >= len) return false;
            in.opcode = bytes[p++];
            entry = has_ib ? OP(I_B, M) : M;
        } else {
            in.map = 2;
            in.opcode = bytes[p++];
            entry = twobyte_entry(in.opcode);
        }
    } else {
        in.map = 1;
        in.opcode = bytes[p++];
        entry = onebyte[in.opcode];
    }

    /* --- ModRM, SIB, displacement --- */
    uint8_t modrm_reg = 0;
    if (entry & M) {
        if (p >= len) return false;
        in.has_modrm = true;
        in.modrm = bytes[p++];

        const uint8_t mod = (uint8_t)(in.modrm >> 6);
        const uint8_t rm  = (uint8_t)(in.modrm & 7);
        modrm_reg = (uint8_t)((in.modrm >> 3) & 7);

        if (mod != 3) {
            if (addrsize16) {
                /* 16-bit addressing: no SIB; disp is 0/1/2 with [disp16] at mod=0,rm=6 */
                if (mod == 1)                    in.disp_size = 1;
                else if (mod == 2)               in.disp_size = 2;
                else if (rm == 6)                in.disp_size = 2;
            } else {
                if (rm == 4) {
                    if (p >= len) return false;
                    in.has_sib = true;
                    in.sib = bytes[p++];
                }
                if (mod == 1)                                        in.disp_size = 1;
                else if (mod == 2)                                   in.disp_size = 4;
                else if (rm == 5)                                    in.disp_size = 4;
                else if (in.has_sib && (in.sib & 7) == 5)            in.disp_size = 4;
            }
        }

        if (p + in.disp_size > len) return false;
        int32_t d = 0;
        for (uint8_t i = 0; i < in.disp_size; i++) d |= (int32_t)bytes[p + i] << (8 * i);
        if (in.disp_size == 1)      d = (int8_t)d;
        else if (in.disp_size == 2) d = (int16_t)d;
        in.disp = d;
        p += in.disp_size;
    }

    /* --- immediate --- */
    in.imm_size = imm_bytes((uint8_t)(entry & 0x7Fu), opsize16, addrsize16, modrm_reg, in.opcode);
    if (p + in.imm_size > len) return false;
    if (in.imm_size && in.imm_size <= 4) {
        int64_t v = 0;
        for (uint8_t i = 0; i < in.imm_size; i++) v |= (int64_t)bytes[p + i] << (8 * i);
        if (in.imm_size == 1)      v = (int8_t)v;
        else if (in.imm_size == 2) v = (int16_t)v;
        else if (in.imm_size == 4) v = (int32_t)v;
        in.imm = v;
    }
    p += in.imm_size;

    if (p > X86_MAX_INSN_LEN) return false;
    in.length = (uint8_t)p;
    *out = in;
    return true;
}
