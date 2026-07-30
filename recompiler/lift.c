/* x86 -> C lifter.
 *
 * Reads lf2.exe and the Ghidra function list, decodes each function with x86_decode,
 * and emits one C function per guest function. Instructions we don't handle yet emit a
 * TODO marker so the output still compiles and coverage is measurable.
 *
 * Usage: lift <lf2.exe> <functions.tsv> <out.c> */
#include "x86_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IMAGE_BASE = 0x400000, MAX_FUNCS = 4096, MAX_INSNS = 8192 };

typedef struct { uint32_t addr, size; } Func;

static uint8_t *image;          /* the whole file */
static uint32_t text_rva, text_size, text_off;
static Func funcs[MAX_FUNCS];
static int nfuncs;

/* Instruction addresses to emit an ESP probe before, from LF2_PROBE at generation time.
 * Inferring frame relationships by hand has been wrong repeatedly in this project; this
 * reports the real value at the real instruction. */
static uint32_t probe_addr[16];
static int nprobes;

static void load_probes(void)
{
    const char *v = getenv("LF2_PROBE");
    if (!v) return;
    while (*v && nprobes < 16) {
        probe_addr[nprobes++] = (uint32_t)strtoul(v, (char **)&v, 16);
        while (*v == ',' || *v == ' ') v++;
    }
}

static int is_probe(uint32_t va)
{
    for (int i = 0; i < nprobes; i++) if (probe_addr[i] == va) return 1;
    return 0;
}

/* A branch to an address INSIDE the function that has no label becomes a tail call,
 * which abandons the frame instead of continuing -- the callee's RET then pops a return
 * address that was never pushed, and the caller's ESP is left wrong. Always a lifter bug,
 * so count and report them. */
static long internal_tailcalls;

static void internal_tailcall(uint32_t fn, uint32_t at, uint32_t target)
{
    internal_tailcalls++;
    if (internal_tailcalls <= 20)
        fprintf(stderr, "  internal tail call: fn_%08x at %08x -> %08x (no label)\n",
                fn, at, target);
}

static uint32_t cur_lo, cur_hi;   /* bounds of the function being lifted */
static const uint32_t *cur_addrs; /* decoded instruction addresses in this function */
static int cur_n;

/* A branch target only gets a label if it is a real decoded instruction boundary.
 * Inline data (jump tables) can desync decoding, leaving targets with no instruction. */
static int has_label(uint32_t t)
{
    for (int i = 0; i < cur_n; i++) if (cur_addrs[i] == t) return 1;
    return 0;
}

static long lifted, todo;
static long todo_by_op[512];

/* ---------- PE ---------- */

static void load_pe(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) { perror(path); exit(2); }
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    rewind(fh);
    image = malloc((size_t)n);
    if (fread(image, 1, (size_t)n, fh) != (size_t)n) { fprintf(stderr, "short read\n"); exit(2); }
    fclose(fh);

    uint32_t pe = *(uint32_t *)(image + 0x3C);
    uint16_t nsec = *(uint16_t *)(image + pe + 6);
    uint16_t optsz = *(uint16_t *)(image + pe + 20);
    uint8_t *sec = image + pe + 24 + optsz;
    for (int i = 0; i < nsec; i++) {
        uint8_t *s = sec + i * 40;
        if (memcmp(s, ".text", 5) == 0) {
            text_rva  = *(uint32_t *)(s + 12);
            text_size = *(uint32_t *)(s + 8);
            text_off  = *(uint32_t *)(s + 20);
        }
    }
}

static const uint8_t *guest_ptr(uint32_t va)
{
    uint32_t rva = va - IMAGE_BASE;
    if (rva < text_rva || rva >= text_rva + text_size) return NULL;
    return image + text_off + (rva - text_rva);
}

static int is_func(uint32_t va)
{
    for (int i = 0; i < nfuncs; i++) if (funcs[i].addr == va) return 1;
    return 0;
}

/* ---------- operand emitters ---------- */

static const char *REG32[8] = { "R(EAX)", "R(ECX)", "R(EDX)", "R(EBX)",
                                "R(ESP)", "R(EBP)", "R(ESI)", "R(EDI)" };

/* Effective address expression for a ModRM that references memory.
 * Returns a bare expression with no surrounding parentheses -- callers wrap it. */
static void ea(char *buf, size_t n, const x86_insn *in)
{
    const uint8_t mod = in->modrm >> 6, rm = in->modrm & 7;
    /* FS addresses the TIB; every other segment is flat in a Win32 process. */
    const char *seg = (in->seg_prefix == 0x64) ? "TIB_BASE + " : "";
    if (mod == 0 && rm == 5) { snprintf(buf, n, "%s0x%xu", seg, (unsigned)in->disp); return; }

    char base[64] = "", index[64] = "";
    if (in->has_sib) {
        const uint8_t b = in->sib & 7, x = (in->sib >> 3) & 7, scale = 1u << (in->sib >> 6);
        if (!(mod == 0 && b == 5)) snprintf(base, sizeof base, "%s", REG32[b]);
        if (x != 4) snprintf(index, sizeof index, " + %s*%u", REG32[x], scale);
    } else {
        snprintf(base, sizeof base, "%s", REG32[rm]);
    }

    char disp[32] = "";
    if (in->disp_size) snprintf(disp, sizeof disp, " + %d", in->disp);
    snprintf(buf, n, "%s%s%s%s", seg, base[0] ? base : "0u", index, disp);
}

/* r/m operand as an rvalue and as an assignment target. */
static void rm_read(char *buf, size_t n, const x86_insn *in, int size)
{
    if ((in->modrm >> 6) == 3) {
        const uint8_t rm = in->modrm & 7;
        if (size == 4) snprintf(buf, n, "%s", REG32[rm]);
        else if (size == 1) snprintf(buf, n, "GETR8(%u)", rm);
        else snprintf(buf, n, "(uint16_t)%s", REG32[rm]);
        return;
    }
    char a[128]; ea(a, sizeof a, in);
    snprintf(buf, n, "LD%d(%s)", size * 8, a);
}

static void rm_write(FILE *o, const x86_insn *in, int size, const char *value)
{
    if ((in->modrm >> 6) == 3) {
        const uint8_t rm = in->modrm & 7;
        if (size == 4)      fprintf(o, "%s = %s;", REG32[rm], value);
        else if (size == 1) fprintf(o, "SETR8(%u, %s);", rm, value);
        else                fprintf(o, "%s = (%s & ~0xffffu) | ((%s) & 0xffffu);",
                                    REG32[rm], REG32[rm], value);
        return;
    }
    char a[128]; ea(a, sizeof a, in);
    fprintf(o, "ST%d(%s, %s);", size * 8, a, value);
}

static const char *reg_operand(const x86_insn *in, int size, char *buf, size_t n)
{
    const uint8_t reg = (in->modrm >> 3) & 7;
    if (size == 4)      snprintf(buf, n, "%s", REG32[reg]);
    else if (size == 1) snprintf(buf, n, "GETR8(%u)", reg);
    else                snprintf(buf, n, "(uint16_t)%s", REG32[reg]);
    return buf;
}

static const char *CC[16] = { "flag_of()", "!flag_of()", "flag_cf()", "!flag_cf()",
    "flag_zf()", "!flag_zf()", "(flag_cf()||flag_zf())", "!(flag_cf()||flag_zf())",
    "flag_sf()", "!flag_sf()", "flag_pf()", "!flag_pf()",
    "(flag_sf()!=flag_of())", "(flag_sf()==flag_of())",
    "(flag_zf()||flag_sf()!=flag_of())", "(!flag_zf()&&flag_sf()==flag_of())" };

/* arithmetic group 0..7 = ADD OR ADC SBB AND SUB XOR CMP */
static const char *ALU_C[8]  = { "+", "|", "+", "-", "&", "-", "^", "-" };
static const char *ALU_F[8]  = { "F_ADD", "F_LOGIC", "F_ADD", "F_SUB",
                                 "F_LOGIC", "F_SUB", "F_LOGIC", "F_SUB" };


/* ---------- x87 ----------
 * Operand width comes from the escape byte plus the ModRM reg field, not the mnemonic.
 * Evaluated in host double; see docs/isa-scope.md for why that is faithful here. */
static const char *FARITH[8] = { "+", "*", "", "", "-", "-", "/", "/" };

static int emit_x87(FILE *o, const x86_insn *in)
{
    const uint8_t esc = in->opcode, modrm = in->modrm;
    const int g = (modrm >> 3) & 7;
    char addr[128];

    if (modrm < 0xC0) {                       /* memory operand */
        ea(addr, sizeof addr, in);
        const char *ld = NULL;
        switch (esc) {
        case 0xD9: ld = "LDF32"; break;
        case 0xDD: ld = "LDF64"; break;
        case 0xD8: ld = "LDF32"; break;
        case 0xDC: ld = "LDF64"; break;
        case 0xDB: ld = (g == 0) ? "(double)(int32_t)LD32" : NULL; break;
        case 0xDF: ld = (g == 0) ? "(double)(int16_t)LD16" : NULL; break;
        case 0xDA: ld = "(double)(int32_t)LD32"; break;
        case 0xDE: ld = "(double)(int16_t)LD16"; break;
        default: break;
        }

        if (esc == 0xD9 || esc == 0xDD) {                 /* FLD / FST / FSTP */
            const char *st = (esc == 0xD9) ? "STF32" : "STF64";
            if (g == 0) { fprintf(o, "fpu_push(%s(%s));", ld, addr); return 1; }
            if (g == 2) { fprintf(o, "%s(%s, FST(0));", st, addr); return 1; }
            if (g == 3) { fprintf(o, "%s(%s, fpu_pop());", st, addr); return 1; }
            return 0;                                     /* FLDCW etc: absent here */
        }
        if (esc == 0xDB || esc == 0xDF) {                 /* FILD / FISTP */
            const int wide = (esc == 0xDF);
            if (g == 0) { fprintf(o, "fpu_push(%s(%s));", ld, addr); return 1; }
            if (g == 2 || g == 3) {
                fprintf(o, "ST%d(%s, (uint%d_t)(int%d_t)%s);", wide ? 16 : 32, addr,
                        wide ? 16 : 32, wide ? 16 : 32, g == 3 ? "fpu_pop()" : "FST(0)");
                return 1;
            }
            if (esc == 0xDF && g == 5) { fprintf(o, "fpu_push((double)(int64_t)LD32(%s));", addr); return 1; }
            if (esc == 0xDF && g == 7) { fprintf(o, "ST32(%s, (uint32_t)(int64_t)fpu_pop());", addr); return 1; }
            return 0;
        }
        if (!ld) return 0;
        if (g == 2 || g == 3) {                           /* FCOM / FCOMP */
            fprintf(o, "fpu_cmp(FST(0), %s(%s));%s", ld, addr, g == 3 ? " fpu_pop();" : "");
            return 1;
        }
        if (!FARITH[g][0]) return 0;
        if (g == 5 || g == 7)                             /* reversed forms */
            fprintf(o, "FST(0) = %s(%s) %s FST(0);", ld, addr, FARITH[g]);
        else
            fprintf(o, "FST(0) = FST(0) %s %s(%s);", FARITH[g], ld, addr);
        return 1;
    }

    /* register forms */
    const int i = modrm & 7;
    switch (esc) {
    case 0xD9:
        if (modrm >= 0xC0 && modrm <= 0xC7) { fprintf(o, "fpu_push(FST(%d));", i); return 1; }
        if (modrm >= 0xC8 && modrm <= 0xCF) {
            fprintf(o, "{ double _t = FST(0); FST(0) = FST(%d); FST(%d) = _t; }", i, i); return 1; }
        if (modrm == 0xE0) { fprintf(o, "FST(0) = -FST(0);"); return 1; }
        if (modrm == 0xE8) { fprintf(o, "fpu_push(1.0);"); return 1; }
        if (modrm == 0xEE) { fprintf(o, "fpu_push(0.0);"); return 1; }
        return 0;
    case 0xD8:
        if (modrm >= 0xD0 && modrm <= 0xD7) { fprintf(o, "fpu_cmp(FST(0), FST(%d));", i); return 1; }
        if (modrm >= 0xD8 && modrm <= 0xDF) { fprintf(o, "fpu_cmp(FST(0), FST(%d)); fpu_pop();", i); return 1; }
        if (!FARITH[g][0]) return 0;
        fprintf(o, "FST(0) = FST(0) %s FST(%d);", FARITH[g], i);
        return 1;
    case 0xDC:
        if (!FARITH[g][0]) return 0;
        /* Reversed at this destination is g=4 (FSUBR) / g=6 (FDIVR) -- the opposite of
         * the D8 forms, where g=5/7 are the reversed ones. Getting it backwards silently
         * yields the reciprocal or the negation of the right answer. */
        if (g == 4 || g == 6) fprintf(o, "FST(%d) = FST(0) %s FST(%d);", i, FARITH[g], i);
        else                  fprintf(o, "FST(%d) = FST(%d) %s FST(0);", i, i, FARITH[g]);
        return 1;
    case 0xDD:
        if (modrm >= 0xD8 && modrm <= 0xDF) { fprintf(o, "FST(%d) = fpu_pop();", i); return 1; }
        if (modrm >= 0xD0 && modrm <= 0xD7) { fprintf(o, "FST(%d) = FST(0);", i); return 1; }
        return 0;
    case 0xDE:
        if (modrm == 0xD9) { fprintf(o, "fpu_cmp(FST(0), FST(1)); fpu_pop(); fpu_pop();"); return 1; }
        if (!FARITH[g][0]) return 0;
        if (g == 4 || g == 6) fprintf(o, "FST(%d) = FST(0) %s FST(%d); fpu_pop();", i, FARITH[g], i);
        else                  fprintf(o, "FST(%d) = FST(%d) %s FST(0); fpu_pop();", i, i, FARITH[g]);
        return 1;
    case 0xDF:
        if (modrm == 0xE0) { fprintf(o, "R(EAX) = (R(EAX) & ~0xffffu) | cpu.fsw;"); return 1; }
        return 0;
    default: return 0;
    }
}

/* ---------- string ops ---------- */
static int emit_string(FILE *o, const x86_insn *in)
{
    const uint8_t op = in->opcode;
    const int size = (op & 1) ? ((in->prefixes & X86_PFX_OPSIZE) ? 2 : 4) : 1;
    const int rep  = (in->prefixes & (X86_PFX_REP | X86_PFX_REPNE)) != 0;
    const int repe = (in->prefixes & X86_PFX_REP) != 0;

    switch (op) {
    case 0xA4: case 0xA5: fprintf(o, "op_movs(%d, %d);", size, rep); return 1;
    case 0xAA: case 0xAB: fprintf(o, "op_stos(%d, %d);", size, rep); return 1;
    case 0xAC: case 0xAD: fprintf(o, "op_lods(%d);", size); return 1;
    case 0xA6: case 0xA7: fprintf(o, "op_cmps(%d, %d);", size, rep ? (repe ? 1 : -1) : 0); return 1;
    case 0xAE: case 0xAF: fprintf(o, "op_scas(%d, %d);", size, rep ? (repe ? 1 : -1) : 0); return 1;
    default: return 0;
    }
}

/* ---------- jump tables ----------
 * A switch compiles to `JMP dword ptr [reg*4 + table]`, and the entries are labels inside
 * the same function, not function entries -- dispatching on them fails. The table is read
 * from the image at generation time and turned into a switch over gotos. */
enum { JT_MAX = 256 };

static int jump_table_targets(const x86_insn *in, uint32_t lo, uint32_t hi,
                              uint32_t *out, int max)
{
    if (in->map != 1 || in->opcode != 0xFF) return 0;
    if (((in->modrm >> 3) & 7) != 4) return 0;                   /* /4 = JMP r/m */
    if ((in->modrm >> 6) != 0 || (in->modrm & 7) != 4) return 0; /* needs a SIB */
    if (!in->has_sib || (in->sib & 7) != 5) return 0;            /* base is the disp32 */
    if ((in->sib >> 6) != 2) return 0;                           /* scale 4 */

    const uint32_t table = (uint32_t)in->disp;
    int n = 0;
    while (n < max) {
        const uint8_t *e = guest_ptr(table + (uint32_t)n * 4);
        if (!e) break;
        const uint32_t t = (uint32_t)e[0] | ((uint32_t)e[1] << 8)
                         | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
        if (t < lo || t >= hi) break;              /* left the function: end of table */
        out[n++] = t;
    }
    return n;
}


/* ---------- instruction emission ---------- */

/* Emit one instruction. Returns 1 if lifted, 0 if it fell through to a TODO. */
static int emit_insn(FILE *o, const x86_insn *in, uint32_t va, uint32_t next)
{
    const uint8_t op = in->opcode;
    const int osize = (in->prefixes & X86_PFX_OPSIZE) ? 2 : 4;
    char a[160], b[160];

    if (in->map == 1) {
        if (op >= 0xD8 && op <= 0xDF) return emit_x87(o, in);
        if ((op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF)) return emit_string(o, in);

        /* ALU r/m,r and r,r/m and al/eax,imm */
        if (op < 0x40 && (op & 7) <= 5 && op != 0x0F &&
            !(op == 0x0F) && ((op & 7) != 6) && ((op & 7) != 7)) {
            const int g = op >> 3;
            const int size = (op & 1) ? osize : 1;
            const int is_cmp = (g == 7), is_test = 0;
            (void)is_test;
            if ((op & 7) <= 1) {                      /* r/m op= r */
                rm_read(a, sizeof a, in, size);
                reg_operand(in, size, b, sizeof b);
                fprintf(o, "{ uint32_t _a=%s, _b=%s, _r=_a %s _b; FLAGS(%s,%d,_a,_b,_r); ",
                        a, b, ALU_C[g], ALU_F[g], size);
                if (!is_cmp) rm_write(o, in, size, "_r");
                fprintf(o, " }");
                return 1;
            }
            if ((op & 7) <= 3) {                      /* r op= r/m */
                reg_operand(in, size, a, sizeof a);
                rm_read(b, sizeof b, in, size);
                fprintf(o, "{ uint32_t _a=%s, _b=%s, _r=_a %s _b; FLAGS(%s,%d,_a,_b,_r); ",
                        a, b, ALU_C[g], ALU_F[g], size);
                if (!is_cmp) {
                    const uint8_t reg = (in->modrm >> 3) & 7;
                    if (size == 4) fprintf(o, "%s = _r;", REG32[reg]);
                    else if (size == 1) fprintf(o, "SETR8(%u, _r);", reg);
                }
                fprintf(o, " }");
                return 1;
            }
            /* AL/eAX, imm */
            const int size2 = (op & 1) ? osize : 1;
            fprintf(o, "{ uint32_t _a=%s, _b=0x%xu, _r=_a %s _b; FLAGS(%s,%d,_a,_b,_r); ",
                    size2 == 1 ? "GETR8(0)" : "R(EAX)", (unsigned)in->imm, ALU_C[g],
                    ALU_F[g], size2);
            if (!is_cmp) fprintf(o, size2 == 1 ? "SETR8(0, _r);" : "R(EAX) = _r;");
            fprintf(o, " }");
            return 1;
        }

        switch (op) {
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            fprintf(o, "PUSH32(%s);", REG32[op - 0x50]); return 1;
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            fprintf(o, "%s = POP32();", REG32[op - 0x58]); return 1;
        case 0x68: fprintf(o, "PUSH32(0x%xu);", (unsigned)in->imm); return 1;
        case 0x6A: fprintf(o, "PUSH32(0x%xu);", (unsigned)in->imm); return 1;

        case 0x88: case 0x89: {
            const int size = (op & 1) ? osize : 1;
            reg_operand(in, size, b, sizeof b);
            rm_write(o, in, size, b);
            return 1;
        }
        case 0x8A: case 0x8B: {
            const int size = (op & 1) ? osize : 1;
            rm_read(a, sizeof a, in, size);
            const uint8_t reg = (in->modrm >> 3) & 7;
            if (size == 4)      fprintf(o, "%s = %s;", REG32[reg], a);
            else if (size == 1) fprintf(o, "SETR8(%u, %s);", reg, a);
            else fprintf(o, "%s = (%s & ~0xffffu) | (%s);", REG32[reg], REG32[reg], a);
            return 1;
        }
        case 0x8D: {                                   /* LEA */
            char addr[128]; ea(addr, sizeof addr, in);
            fprintf(o, "%s = %s;", REG32[(in->modrm >> 3) & 7], addr);
            return 1;
        }
        case 0x8F: rm_write(o, in, 4, "POP32()"); return 1;

        case 0x90: fprintf(o, "/* nop */"); return 1;
        case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97:
            fprintf(o, "{ uint32_t _t=R(EAX); R(EAX)=%s; %s=_t; }",
                    REG32[op - 0x90], REG32[op - 0x90]); return 1;
        case 0x8C:  /* MOV r/m16, Sreg -- flat model, selectors are inert here */
            rm_write(o, in, 2, "0u"); return 1;
        case 0x8E: fprintf(o, "/* MOV Sreg, r/m16 ignored (flat model) */"); return 1;
        case 0x9C: fprintf(o, "PUSH32(flags_pack());"); return 1;
        case 0x9D: fprintf(o, "flags_unpack(POP32());"); return 1;
        case 0xFE: {
            const int g2 = (in->modrm >> 3) & 7;
            rm_read(a, sizeof a, in, 1);
            fprintf(o, "{ uint32_t _a=%s,_r=_a%s1; FLAGS(%s,1,_a,1,_r); ",
                    a, g2 == 0 ? "+" : "-", g2 == 0 ? "F_INC" : "F_DEC");
            rm_write(o, in, 1, "_r");
            fprintf(o, " }");
            return 1;
        }

        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            fprintf(o, "SETR8(%u, 0x%xu);", op - 0xB0, (unsigned)in->imm); return 1;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            fprintf(o, "%s = 0x%xu;", REG32[op - 0xB8], (unsigned)in->imm); return 1;

        case 0x80: case 0x81: case 0x83: {             /* ALU r/m, imm */
            const int g = (in->modrm >> 3) & 7;
            const int size = (op == 0x80) ? 1 : osize;
            rm_read(a, sizeof a, in, size);
            fprintf(o, "{ uint32_t _a=%s, _b=0x%xu, _r=_a %s _b; FLAGS(%s,%d,_a,_b,_r); ",
                    a, (unsigned)in->imm, ALU_C[g], ALU_F[g], size);
            if (g != 7) rm_write(o, in, size, "_r");
            fprintf(o, " }");
            return 1;
        }
        case 0x84: case 0x85: {                        /* TEST r/m, r */
            const int size = (op & 1) ? osize : 1;
            rm_read(a, sizeof a, in, size);
            reg_operand(in, size, b, sizeof b);
            fprintf(o, "{ uint32_t _a=%s,_b=%s; FLAGS(F_LOGIC,%d,_a,_b,_a & _b); }", a, b, size);
            return 1;
        }
        case 0xA8: case 0xA9: {
            const int size = (op & 1) ? osize : 1;
            fprintf(o, "{ uint32_t _a=%s,_b=0x%xu; FLAGS(F_LOGIC,%d,_a,_b,_a & _b); }",
                    size == 1 ? "GETR8(0)" : "R(EAX)", (unsigned)in->imm, size);
            return 1;
        }

        case 0xC6: case 0xC7: {
            const int size = (op & 1) ? osize : 1;
            char v[32]; snprintf(v, sizeof v, "0x%xu", (unsigned)in->imm);
            rm_write(o, in, size, v);
            return 1;
        }

        case 0xA0: case 0xA1:                          /* MOV AL/eAX, moffs */
            if (op == 0xA0) fprintf(o, "SETR8(0, LD8(0x%xu));", (unsigned)in->imm);
            else            fprintf(o, "R(EAX) = LD32(0x%xu);", (unsigned)in->imm);
            return 1;
        case 0xA2: case 0xA3:                          /* MOV moffs, AL/eAX */
            if (op == 0xA2) fprintf(o, "ST8(0x%xu, GETR8(0));", (unsigned)in->imm);
            else            fprintf(o, "ST32(0x%xu, R(EAX));", (unsigned)in->imm);
            return 1;

        case 0x69: case 0x6B: {                        /* IMUL r32, r/m32, imm */
            rm_read(a, sizeof a, in, 4);
            const uint8_t reg = (in->modrm >> 3) & 7;
            fprintf(o, "%s = (uint32_t)((int32_t)(%s) * %d);", REG32[reg], a, (int)in->imm);
            return 1;
        }
        case 0x86: case 0x87: {                        /* XCHG */
            const int size = (op & 1) ? osize : 1;
            rm_read(a, sizeof a, in, size);
            reg_operand(in, size, b, sizeof b);
            fprintf(o, "{ uint32_t _t=%s; ", a);
            rm_write(o, in, size, b);
            const uint8_t reg = (in->modrm >> 3) & 7;
            if (size == 4) fprintf(o, " %s = _t; }", REG32[reg]);
            else           fprintf(o, " SETR8(%u, _t); }", reg);
            return 1;
        }

        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            const int g = (in->modrm >> 3) & 7;
            const int size = (op & 1) ? osize : 1;
            char cnt[32];
            if (op == 0xC0 || op == 0xC1) snprintf(cnt, sizeof cnt, "%u", (unsigned)(in->imm & 31));
            else if (op == 0xD0 || op == 0xD1) snprintf(cnt, sizeof cnt, "1");
            else snprintf(cnt, sizeof cnt, "(GETR8(1) & 31)");
            rm_read(a, sizeof a, in, size);
            const char *fn = (g == 4 || g == 6) ? "shl" : (g == 5) ? "shr" : (g == 7) ? "sar"
                           : (g == 0) ? "rol" : (g == 1) ? "ror" : NULL;
            if (!fn) break;                            /* RCL/RCR: not seen in this binary */
            fprintf(o, "{ uint32_t _r = op_%s%d(%s, %s); ", fn, size * 8, a, cnt);
            rm_write(o, in, size, "_r");
            fprintf(o, " }");
            return 1;
        }

        case 0xF6: case 0xF7: {
            const int g = (in->modrm >> 3) & 7;
            const int size = (op & 1) ? osize : 1;
            rm_read(a, sizeof a, in, size);
            switch (g) {
            case 0: case 1:                            /* TEST r/m, imm */
                fprintf(o, "{ uint32_t _a=%s,_b=0x%xu; FLAGS(F_LOGIC,%d,_a,_b,_a & _b); }",
                        a, (unsigned)in->imm, size);
                return 1;
            case 2:                                    /* NOT */
                fprintf(o, "{ uint32_t _r = ~(uint32_t)(%s); ", a);
                rm_write(o, in, size, "_r"); fprintf(o, " }"); return 1;
            case 3:                                    /* NEG */
                fprintf(o, "{ uint32_t _a=%s,_r=0u-_a; FLAGS(F_SUB,%d,0u,_a,_r); ", a, size);
                rm_write(o, in, size, "_r"); fprintf(o, " }"); return 1;
            case 4:                                    /* MUL */
                if (size == 1) { fprintf(o, "{ uint32_t _p=(uint32_t)GETR8(0)*(uint32_t)(%s); "
                                            "R(EAX)=(R(EAX)&~0xffffu)|(_p&0xffffu); }", a); return 1; }
                if (size != 4) break;
                fprintf(o, "{ uint64_t _p=(uint64_t)R(EAX)*(uint64_t)(%s); "
                           "R(EAX)=(uint32_t)_p; R(EDX)=(uint32_t)(_p>>32); }", a);
                return 1;
            case 5:                                    /* IMUL */
                if (size == 1) { fprintf(o, "{ int32_t _p=(int32_t)(int8_t)GETR8(0)*(int32_t)(int8_t)(%s); "
                                            "R(EAX)=(R(EAX)&~0xffffu)|((uint32_t)_p&0xffffu); }", a); return 1; }
                if (size != 4) break;
                fprintf(o, "{ int64_t _p=(int64_t)(int32_t)R(EAX)*(int64_t)(int32_t)(%s); "
                           "R(EAX)=(uint32_t)_p; R(EDX)=(uint32_t)((uint64_t)_p>>32); }", a);
                return 1;
            case 6:                                    /* DIV */
                if (size == 1) { fprintf(o, "{ uint32_t _n=R(EAX)&0xffffu, _d=%s; "
                                            "R(EAX)=(R(EAX)&~0xffffu)|((_n/_d)&0xffu)|(((_n%%_d)&0xffu)<<8); }", a); return 1; }
                if (size != 4) break;
                fprintf(o, "{ uint64_t _n=((uint64_t)R(EDX)<<32)|R(EAX); uint32_t _d=%s; "
                           "R(EAX)=(uint32_t)(_n/_d); R(EDX)=(uint32_t)(_n%%_d); }", a);
                return 1;
            case 7:                                    /* IDIV */
                if (size == 1) { fprintf(o, "{ int32_t _n=(int16_t)R(EAX), _d=(int8_t)(%s); "
                                            "R(EAX)=(R(EAX)&~0xffffu)|((uint32_t)(_n/_d)&0xffu)|(((uint32_t)(_n%%_d)&0xffu)<<8); }", a); return 1; }
                if (size != 4) break;
                fprintf(o, "{ int64_t _n=(int64_t)(((uint64_t)R(EDX)<<32)|R(EAX)); "
                           "int32_t _d=(int32_t)(%s); "
                           "R(EAX)=(uint32_t)(int32_t)(_n/_d); R(EDX)=(uint32_t)(int32_t)(_n%%_d); }", a);
                return 1;
            default: break;
            }
            break;
        }

        case 0x98: fprintf(o, "R(EAX) = (uint32_t)(int32_t)(int16_t)R(EAX);"); return 1;
        case 0x99: fprintf(o, "R(EDX) = (uint32_t)((int32_t)R(EAX) >> 31);"); return 1;
        case 0xC9: fprintf(o, "R(ESP) = R(EBP); R(EBP) = POP32();"); return 1;

        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47:
            fprintf(o, "{ uint32_t _a=%s,_r=_a+1; FLAGS(F_INC,4,_a,1,_r); %s=_r; }",
                    REG32[op - 0x40], REG32[op - 0x40]); return 1;
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            fprintf(o, "{ uint32_t _a=%s,_r=_a-1; FLAGS(F_DEC,4,_a,1,_r); %s=_r; }",
                    REG32[op - 0x48], REG32[op - 0x48]); return 1;

        case 0xE8: {                                   /* CALL rel32 */
            uint32_t t = (uint32_t)(next + (int32_t)in->imm);
            fprintf(o, "PUSH32(0x%xu); ", next);
            if (is_func(t)) fprintf(o, "fn_%08x();", t);
            else            fprintf(o, "dispatch(0x%xu);", t);
            return 1;
        }
        case 0xE9: case 0xEB: {                        /* JMP rel */
            uint32_t t = (uint32_t)(next + (int32_t)in->imm);
            if (t >= cur_lo && t < cur_hi && has_label(t)) fprintf(o, "goto L_%08x;", t);
            else if (is_func(t))           fprintf(o, "fn_%08x(); return;", t);
            else                           fprintf(o, "dispatch(0x%xu); return;", t);
            if (t >= cur_lo && t < cur_hi && !has_label(t)) internal_tailcall(cur_lo, va, t);
            return 1;
        }
        case 0xC3:
            fprintf(o, "STACK_CHECK(_esp0, 0x%xu); R(ESP) += 4; return;", cur_lo);
            return 1;
        case 0xC2:
            fprintf(o, "STACK_CHECK(_esp0, 0x%xu); R(ESP) += %u; return;",
                    cur_lo, 4u + (unsigned)in->imm);
            return 1;

        case 0xFF: {
            const int g = (in->modrm >> 3) & 7;
            if (g == 6) { rm_read(a, sizeof a, in, 4); fprintf(o, "PUSH32(%s);", a); return 1; }
            if (g == 2) { rm_read(a, sizeof a, in, 4);
                          fprintf(o, "PUSH32(0x%xu); dispatch(%s);", next, a); return 1; }
            if (g == 4) {
                rm_read(a, sizeof a, in, 4);
                uint32_t jt[JT_MAX];
                const int njt = jump_table_targets(in, cur_lo, cur_hi, jt, JT_MAX);
                if (njt > 0) {
                    fprintf(o, "{ uint32_t _t = %s; switch (_t) {", a);
                    for (int k = 0; k < njt; k++) {
                        int dup = 0;
                        for (int j = 0; j < k; j++) if (jt[j] == jt[k]) { dup = 1; break; }
                        if (!dup) fprintf(o, " case 0x%xu: goto L_%08x;", jt[k], jt[k]);
                    }
                    fprintf(o, " default: dispatch(_t); return; } }");
                    return 1;
                }
                fprintf(o, "dispatch(%s); return;", a);
                return 1;
            }
            if (g == 0 || g == 1) {
                rm_read(a, sizeof a, in, 4);
                fprintf(o, "{ uint32_t _a=%s,_r=_a%s1; FLAGS(%s,4,_a,1,_r); ",
                        a, g == 0 ? "+" : "-", g == 0 ? "F_INC" : "F_DEC");
                rm_write(o, in, 4, "_r");
                fprintf(o, " }");
                return 1;
            }
            break;
        }
        default: break;
        }

        if (op >= 0x70 && op <= 0x7F) {                /* Jcc rel8 */
            uint32_t t = (uint32_t)(next + (int32_t)in->imm);
            if (t >= cur_lo && t < cur_hi && has_label(t)) fprintf(o, "if (%s) goto L_%08x;", CC[op - 0x70], t);
            else {
                fprintf(o, "if (%s) { dispatch(0x%xu); return; }", CC[op - 0x70], t);
                if (t >= cur_lo && t < cur_hi) internal_tailcall(cur_lo, va, t);
            }
            return 1;
        }
    } else if (in->map == 2) {
        if (op >= 0x80 && op <= 0x8F) {                /* Jcc rel32 */
            uint32_t t = (uint32_t)(next + (int32_t)in->imm);
            if (t >= cur_lo && t < cur_hi && has_label(t)) fprintf(o, "if (%s) goto L_%08x;", CC[op - 0x80], t);
            else {
                fprintf(o, "if (%s) { dispatch(0x%xu); return; }", CC[op - 0x80], t);
                if (t >= cur_lo && t < cur_hi) internal_tailcall(cur_lo, va, t);
            }
            return 1;
        }
        if (op >= 0x90 && op <= 0x9F) {                /* SETcc */
            char v[64]; snprintf(v, sizeof v, "(%s) ? 1u : 0u", CC[op - 0x90]);
            rm_write(o, in, 1, v);
            return 1;
        }
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF) {  /* MOVZX / MOVSX */
            const int size = (op & 1) ? 2 : 1;
            rm_read(a, sizeof a, in, size);
            const uint8_t reg = (in->modrm >> 3) & 7;
            /* With the operand-size prefix the destination is 16-bit, so the upper half
             * of the register is preserved rather than cleared. */
            if (osize == 2) {
                if (op < 0xBE)
                    fprintf(o, "%s = (%s & ~0xffffu) | ((uint32_t)(%s) & 0xffffu);",
                            REG32[reg], REG32[reg], a);
                else
                    fprintf(o, "%s = (%s & ~0xffffu) | ((uint32_t)(int32_t)(int%d_t)(%s) & 0xffffu);",
                            REG32[reg], REG32[reg], size * 8, a);
                return 1;
            }
            if (op < 0xBE) fprintf(o, "%s = (uint32_t)(%s);", REG32[reg], a);
            else fprintf(o, "%s = (uint32_t)(int32_t)(int%d_t)(%s);", REG32[reg], size * 8, a);
            return 1;
        }
        if (op == 0xA2) { fprintf(o, "op_cpuid();"); return 1; }
        if (op == 0x28 || op == 0x29) { fprintf(o, "/* MOVAPD: CRT scratch, no guest effect */"); return 1; }
        if (op == 0x2C || op == 0x2D) {                 /* CVTTSD2SI */
            rm_read(a, sizeof a, in, 4);
            fprintf(o, "%s = (uint32_t)(int32_t)LDF64(%s);", REG32[(in->modrm >> 3) & 7], a);
            return 1;
        }
        if (op == 0xAF) {                               /* IMUL r32, r/m32 */
            rm_read(a, sizeof a, in, 4);
            const uint8_t reg = (in->modrm >> 3) & 7;
            fprintf(o, "%s = (uint32_t)((int32_t)%s * (int32_t)(%s));", REG32[reg], REG32[reg], a);
            return 1;
        }
    }
    return 0;
}

/* ---------- per-function driver ---------- */

static void lift_function(FILE *o, const Func *f)
{
    const uint8_t *code = guest_ptr(f->addr);
    if (!code) return;

    cur_lo = f->addr;
    /* Extend to the next entry rather than trusting the declared size: Ghidra's sizes are
     * occasionally a byte or two short, which leaves a trailing instruction undecoded. A
     * branch to it then has no label and is emitted as a tail call to an address nothing
     * defines, which aborts at runtime if that path is ever taken. */
    cur_hi = f->addr + f->size;

    /* Ghidra's declared sizes are not reliable: a body can continue past the end it
     * reports, and derive_entries may have planted a synthetic entry mid-body, so neither
     * the size nor the next entry marks the real end. Follow the control flow instead:
     * keep going while the code falls through, and stop only once past the declared end,
     * after a terminator, with no forward branch still pointing further on. INT3 padding
     * ends it unconditionally.
     *
     * Getting this wrong is not cosmetic. If the emitted body stops early, control runs
     * off the end of the generated function and returns with the frame still allocated. */
    {
        const uint32_t declared_end = f->addr + f->size;
        uint32_t va = f->addr, furthest = 0;
        int terminated = 0;
        while (va < f->addr + 0x8000) {
            const uint8_t *p = guest_ptr(va);
            if (!p) break;
            if (*p == 0xCC && va >= declared_end) break;      /* padding */
            x86_insn in;
            if (!x86_decode(p, 16, &in)) break;
            const uint32_t next = va + in.length;

            const int uncond_jmp = (in.map == 1 && (in.opcode == 0xE9 || in.opcode == 0xEB));
            const int is_ret = (in.map == 1 && (in.opcode == 0xC2 || in.opcode == 0xC3));
            const int cond = (in.map == 1 && in.opcode >= 0x70 && in.opcode <= 0x7F) ||
                             (in.map == 2 && in.opcode >= 0x80 && in.opcode <= 0x8F);
            if (uncond_jmp || cond) {
                const uint32_t t = (uint32_t)(next + (int32_t)in.imm);
                if (t > furthest && t < f->addr + 0x8000) furthest = t;
            }
            terminated = uncond_jmp || is_ret;
            va = next;
            if (va >= declared_end && terminated && va > furthest) break;
        }
        if (va > cur_hi) cur_hi = va;
    }

    static uint32_t addrs[MAX_INSNS];
    static x86_insn insns[MAX_INSNS];
    static uint8_t is_target[MAX_INSNS];
    int n = 0;

    uint32_t va = f->addr;
    while (va < cur_hi && n < MAX_INSNS) {
        const uint8_t *p = guest_ptr(va);
        if (!p) break;
        /* MSVC pads between functions with INT3. Decoding it produces thousands of bogus
         * instructions, so the extension past the declared size stops there. */
        if (va >= f->addr + f->size && *p == 0xCC) break;
        x86_insn in;
        size_t avail = cur_hi - va;
        if (!x86_decode(p, avail < 16 ? avail : 16, &in)) break;
        addrs[n] = va;
        insns[n] = in;
        va += in.length;
        n++;
    }

    /* mark intra-function branch targets so we only emit labels that are used */
    memset(is_target, 0, sizeof(uint8_t) * (size_t)n);
    for (int i = 0; i < n; i++) {
        const x86_insn *in = &insns[i];
        uint32_t next = addrs[i] + in->length, t = 0;
        int branch = 0;
        if (in->map == 1 && (in->opcode == 0xE9 || in->opcode == 0xEB ||
                             (in->opcode >= 0x70 && in->opcode <= 0x7F))) branch = 1;
        if (in->map == 2 && in->opcode >= 0x80 && in->opcode <= 0x8F) branch = 1;
        if (!branch) {
            uint32_t jt[JT_MAX];
            const int njt = jump_table_targets(in, cur_lo, cur_hi, jt, JT_MAX);
            for (int k = 0; k < njt; k++)
                for (int j = 0; j < n; j++)
                    if (addrs[j] == jt[k]) { is_target[j] = 1; break; }
            continue;
        }
        t = (uint32_t)(next + (int32_t)in->imm);
        for (int j = 0; j < n; j++) if (addrs[j] == t) { is_target[j] = 1; break; }
    }

    cur_addrs = addrs;
    cur_n = n;

    fprintf(o, "\nvoid fn_%08x(void)\n{\n", f->addr);
    fprintf(o, "    FN_ENTER(0x%xu);\n", f->addr);
    fprintf(o, "    const uint32_t _esp0 = R(ESP); (void)_esp0;\n");
    for (int i = 0; i < n; i++) {
        if (is_target[i]) fprintf(o, "L_%08x:\n", addrs[i]);
        if (is_probe(addrs[i])) fprintf(o, "    PROBE(0x%xu);\n", addrs[i]);
        fprintf(o, "    ");
        if (emit_insn(o, &insns[i], addrs[i], addrs[i] + insns[i].length)) {
            lifted++;
        } else {
            todo++;
            todo_by_op[insns[i].map == 2 ? 256 + insns[i].opcode : insns[i].opcode]++;
            fprintf(o, "TODO(\"%02x%s\");", insns[i].opcode, insns[i].map == 2 ? " 0f" : "");
        }
        fprintf(o, "\n");
    }
    fprintf(o, "    FELL_OFF_END(0x%xu);\n}\n", f->addr);
}

/* ---- instruction differential test generator ----
 *
 * Emits one C function per distinct register-only instruction encoding in the corpus,
 * each containing exactly what the lifter would emit for it. The test harness runs that
 * against the real instruction executed on the host with identical inputs. Flags are
 * already covered separately; this checks the VALUES.
 *
 * Restricted to register-only forms (ModRM mod == 3, or no ModRM) so no guest addressing
 * is involved, and to non-control-flow so execution stays straight-line. */
static int testable(const x86_insn *in)
{
    const uint8_t op = in->opcode;

    /* Register-only, for every opcode map. A memory ModRM is not merely awkward here:
     * mod=00 rm=101 means absolute disp32 in 32-bit code but RIP-relative in long mode,
     * so the host would execute something entirely different. */
    if (!in->has_modrm && in->map == 2) return 0;

    if (in->has_modrm && (in->modrm >> 6) != 3) {
        const uint8_t mod = in->modrm >> 6, rm = in->modrm & 7;
        /* mod=00 rm=101 is absolute disp32 in 32-bit code and RIP-relative in long mode,
         * so the host would address something else entirely. */
        if (mod == 0 && rm == 5) return 0;
        if (in->has_sib) {
            if ((in->sib & 7) == 5 && mod == 0) return 0;   /* no base register */
            if (((in->sib >> 3) & 7) == 4) { /* no index: fine */ }
        }
        /* The address registers must not be ESP, which the harness cannot hand over. */
        if (!in->has_sib && rm == 4) return 0;
        /* Base and index being the same register defeats the harness: it offsets that
         * register by the mapping base, which then gets multiplied by the scale too. */
        if (in->has_sib && (in->sib & 7) == ((in->sib >> 3) & 7)) return 0;
        if (in->has_sib && ((in->sib & 7) == 4 || ((in->sib >> 3) & 7) == 4)) {
            if ((in->sib & 7) == 4) return 0;
        }
    }

    /* Anything naming ESP as a data operand. */
    if (in->has_modrm) {
        if ((in->modrm >> 6) == 3 && (in->modrm & 7) == 4) return 0;
        if (((in->modrm >> 3) & 7) == 4) return 0;
    }

    if (in->map == 2) {
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF || op == 0xAF) return 1;
        if (op >= 0x90 && op <= 0x9F) return 1;          /* SETcc */
        return 0;
    }
    if (in->map != 1) return 0;

    if (op >= 0xA0 && op <= 0xA3) return 0;              /* MOV moffs: no ModRM, absolute */
    /* String ops are testable: they address through ESI/EDI, which the harness can
     * offset the same way it offsets a ModRM base. */
    if (op == 0xE8 || op == 0xE9 || op == 0xEB) return 0;
    if (op >= 0x70 && op <= 0x7F) return 0;
    if (op == 0xC2 || op == 0xC3 || op == 0xC9) return 0;
    if (op == 0xFF || op == 0xFE) return 0;
    /* PUSH and POP cannot be differentially tested against a 64-bit host at all: in long
     * mode their operand size defaults to 64, so the CPU moves eight bytes and adjusts
     * rsp by eight where 32-bit code moves four. No encoding overrides that, so the two
     * sides cannot be made to agree. This is a limit of the technique, not a gap that
     * more harness work would close -- an interpreter-based oracle would be needed. */
    if (op >= 0x50 && op <= 0x5F) return 0;
    if (op == 0x68 || op == 0x6A || op == 0x8F) return 0;
    if (op == 0x9C || op == 0x9D) return 0;
    if (op == 0xCC || op == 0xCD) return 0;
    if (op >= 0x40 && op <= 0x4F) return 0;              /* REX prefixes in long mode */
    if (op >= 0x90 && op <= 0x97) return 0;              /* XCHG with eAX, and NOP */
    if (op >= 0xB8 && op <= 0xBF && (op & 7) == 4) return 0;
    if (op >= 0xB0 && op <= 0xB7 && (op & 7) == 4) return 0;

    /* DIV and IDIV fault on a zero or overflowing divisor, which random inputs produce
     * constantly. They need a test with controlled operands, not this one. */
    if ((op == 0xF6 || op == 0xF7) && (((in->modrm >> 3) & 7) >= 6)) return 0;
    return 1;
}

static void gen_insn_test(const char *tsv, const char *out)
{
    FILE *in_f = fopen(tsv, "r");
    FILE *o = fopen(out, "w");
    if (!in_f || !o) { perror("insn-test"); exit(2); }

    fprintf(o, "/* generated by lift --insn-test -- do not edit */\n");
    fprintf(o, "#include \"guest_ops.h\"\n#include \"insn_test.h\"\n\n");

    static char seen[1 << 16][32];
    int nseen = 0, emitted = 0;
    char line[1024];

    fprintf(o, "static const InsnCase cases[] = {\n");
    long pos_table = ftell(o);
    (void)pos_table;
    fclose(o);
    o = fopen(out, "w");
    fprintf(o, "/* generated by lift --insn-test -- do not edit */\n");
    fprintf(o, "#include \"guest_ops.h\"\n#include \"insn_test.h\"\n\n");

    /* pass 1: bodies */
    while (fgets(line, sizeof line, in_f)) {
        char *f1 = strtok(line, "\t"), *f2 = strtok(NULL, "\t");
        char *f3 = strtok(NULL, "\t"), *f4 = strtok(NULL, "\t");
        if (!f1 || !f2 || !f3 || !f4) continue;

        uint8_t bytes[16];
        size_t n = 0;
        for (const char *q = f4; q[0] && q[1] && n < sizeof bytes; q += 2) {
            unsigned v;
            if (sscanf(q, "%2x", &v) != 1) break;
            bytes[n++] = (uint8_t)v;
        }
        if (!n) continue;

        x86_insn insn;
        if (!x86_decode(bytes, n, &insn) || !testable(&insn)) continue;

        int dup = 0;
        for (int i = 0; i < nseen; i++) if (strcmp(seen[i], f4) == 0) { dup = 1; break; }
        if (dup || nseen >= (1 << 16)) continue;
        snprintf(seen[nseen++], 32, "%s", f4);

        fprintf(o, "static void case_%d(void)\n{\n    ", emitted);
        cur_lo = 0; cur_hi = 0xffffffffu; cur_addrs = NULL; cur_n = 0;
        if (!emit_insn(o, &insn, 0x400000, 0x400000 + insn.length)) {
            fprintf(o, "/* unlifted */");
        }
        fprintf(o, "\n}\n\n");
        emitted++;
    }

    /* pass 2: table */
    fprintf(o, "const InsnCase insn_cases[] = {\n");
    rewind(in_f);
    int idx = 0;
    nseen = 0;
    while (fgets(line, sizeof line, in_f)) {
        char *f1 = strtok(line, "\t"), *f2 = strtok(NULL, "\t");
        char *f3 = strtok(NULL, "\t"), *f4 = strtok(NULL, "\t");
        if (!f1 || !f2 || !f3 || !f4) continue;
        uint8_t bytes[16];
        size_t n = 0;
        for (const char *q = f4; q[0] && q[1] && n < sizeof bytes; q += 2) {
            unsigned v;
            if (sscanf(q, "%2x", &v) != 1) break;
            bytes[n++] = (uint8_t)v;
        }
        if (!n) continue;
        x86_insn insn;
        if (!x86_decode(bytes, n, &insn) || !testable(&insn)) continue;
        int dup = 0;
        for (int i = 0; i < nseen; i++) if (strcmp(seen[i], f4) == 0) { dup = 1; break; }
        if (dup) continue;
        snprintf(seen[nseen++], 32, "%s", f4);

        int base = -1, index = -1, mem = 0;
        if (insn.has_modrm && (insn.modrm >> 6) != 3) {
            mem = 1;
            if (insn.has_sib) {
                base = insn.sib & 7;
                if (((insn.sib >> 3) & 7) != 4) index = (insn.sib >> 3) & 7;
            } else {
                base = insn.modrm & 7;
            }
        }
        fprintf(o, "    { \"%s\", %u, { ", f3, (unsigned)n);
        for (size_t i = 0; i < n; i++) fprintf(o, "0x%02x, ", bytes[i]);
        /* LEA puts the computed ADDRESS in a register, so that register holds a guest
         * address on one side and a host address on the other; the harness must add the
         * mapping base rather than skip it. */
        const int addr_reg = (insn.map == 1 && insn.opcode == 0x8D)
                           ? (int)((insn.modrm >> 3) & 7) : -1;
        const int scale = insn.has_sib ? (1 << (insn.sib >> 6)) : 1;
        const int is_x87 = (insn.map == 1 && insn.opcode >= 0xD8 && insn.opcode <= 0xDF);
        const int is_stack = (insn.map == 1 &&
                             ((insn.opcode >= 0x50 && insn.opcode <= 0x5F) ||
                              insn.opcode == 0x68 || insn.opcode == 0x6A ||
                              insn.opcode == 0x8F ||
                              (insn.opcode == 0xFF && ((insn.modrm >> 3) & 7) == 6)));
        const int is_str = (insn.map == 1 &&
                            ((insn.opcode >= 0xA4 && insn.opcode <= 0xA7) ||
                             (insn.opcode >= 0xAA && insn.opcode <= 0xAF)));
        fprintf(o, "}, case_%d, %d, %d, %d, %d, %d, %d, %d, %d, %d },\n",
                idx++, mem, base, index, addr_reg, (int)insn.disp, scale,
                is_x87, is_str, is_stack);
    }
    fprintf(o, "};\nconst int insn_ncases = %d;\n", idx);
    fclose(o);
    fclose(in_f);
    printf("emitted %d distinct register-only instruction cases\n", idx);
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--insn-test") == 0) {
        gen_insn_test(argv[2], argv[3]);
        return 0;
    }

    if (argc != 4) {
        fprintf(stderr, "usage: %s <lf2.exe> <functions.tsv> <out.c>\n", argv[0]);
        return 2;
    }
    load_probes();
    load_pe(argv[1]);

    FILE *fl = fopen(argv[2], "r");
    if (!fl) { perror(argv[2]); return 2; }
    char line[512];
    while (fgets(line, sizeof line, fl) && nfuncs < MAX_FUNCS) {
        uint32_t addr = (uint32_t)strtoul(line, NULL, 16);
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        uint32_t size = (uint32_t)strtoul(tab + 1, NULL, 10);
        if (!addr || !size) continue;
        funcs[nfuncs].addr = addr;
        funcs[nfuncs].size = size;
        nfuncs++;
    }
    fclose(fl);

    for (int i = 1; i < nfuncs; i++) {          /* insertion sort by address */
        Func k = funcs[i];
        int j = i - 1;
        while (j >= 0 && funcs[j].addr > k.addr) { funcs[j + 1] = funcs[j]; j--; }
        funcs[j + 1] = k;
    }

    FILE *o = fopen(argv[3], "w");
    if (!o) { perror(argv[3]); return 2; }
    fprintf(o, "/* generated by recompiler/lift.c -- do not edit */\n");
    fprintf(o, "#include \"guest.h\"\n#include \"guest_ops.h\"\n");
    for (int i = 0; i < nfuncs; i++) fprintf(o, "void fn_%08x(void);\n", funcs[i].addr);
    for (int i = 0; i < nfuncs; i++) lift_function(o, &funcs[i]);

    /* Address -> function table for indirect calls. Sorted, so dispatch binary-searches. */
    fprintf(o, "\nconst GuestFunc g_funcs[] = {\n");
    for (int i = 0; i < nfuncs; i++)
        fprintf(o, "    { 0x%xu, fn_%08x },\n", funcs[i].addr, funcs[i].addr);
    fprintf(o, "};\nconst int g_nfuncs = %d;\n", nfuncs);
    fclose(o);

    const long total = lifted + todo;
    printf("%d functions, %ld instructions: %ld lifted (%.2f%%), %ld TODO\n",
           nfuncs, total, lifted, total ? lifted * 100.0 / (double)total : 0.0, todo);

    if (internal_tailcalls)
        printf("!! %ld branches to unlabelled addresses inside their own function\n",
               internal_tailcalls);
    printf("top unhandled:");
    for (int round = 0; round < 12; round++) {
        int best = -1;
        for (int i = 0; i < 512; i++) if (todo_by_op[i] && (best < 0 || todo_by_op[i] > todo_by_op[best])) best = i;
        if (best < 0) break;
        printf(" %s%02x(%ld)", best >= 256 ? "0f" : "", best & 0xff, todo_by_op[best]);
        todo_by_op[best] = 0;
    }
    printf("\n");
    return 0;
}
