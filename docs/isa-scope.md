# ISA scope — what the recompiler actually has to handle

Measured, not assumed. Ghidra disassembled all of `.text` and
`tools/ghidra/DumpInstructions.java` dumped every instruction to
`re/instructions.tsv` (address, length, mnemonic, bytes). That file is both the
**scoping data** and the decoder's **test oracle**: our decoder must agree with Ghidra on
the length of all 70,508 instructions.

```sh
analyzeHeadless scratch/ghidra lf2 -process lf2.exe -noanalysis \
  -scriptPath tools/ghidra -postScript DumpInstructions.java $PWD/re/instructions.tsv
```

## Headline: the subset is small

**70,508 instructions, only 92 distinct mnemonics.**

| Top N mnemonics | Coverage |
|---|---|
| 10 | 81.95% |
| 20 | 94.24% |
| 30 | 97.23% |
| 50 | 99.50% |
| 60 | 99.81% |

`MOV` alone is 34% (24,303). The next nine are `PUSH`, `CMP`, `ADD`, `JNZ`, `CALL`,
`LEA`, `JZ`, `POP`, `JMP`. This is plain integer/branch code — no MMX, no vector maths,
nothing exotic.

## Where the difficulty actually is

### x87 FPU — 2273 instructions (3.22%), 25 mnemonics

`FSTP`(734) `FLD`(470) `FILD`(187) `FNSTSW`(177) `FCOM`(114) `FST`(110) `FXCH`(98)
`FADD`(74) `FLDZ`(73) `FCOMP`(62) `FSUB`(34) `FCHS`(32) `FMUL`(31) … plus
`FDIV/FDIVR/FSUBR/FADDP/FMULP/FSUBP/FDIVP/FSUBRP/FDIVRP/FCOMPP/FISTP/FLD1`.

**This is the hard part of the whole recompiler.** x87 is a register *stack* with 80-bit
extended-precision intermediates. `FXCH` (98 sites) rotates that stack, and `FNSTSW`
(177 sites) reads the FPU status word — the classic pre-`FCOMI` float-compare idiom, so
float comparisons flow through the status word rather than EFLAGS.

Bit-exactness is at stake: if the game computes positions in x87 and we evaluate in
host `double` (64-bit mantissa vs 80-bit), results can diverge. Decide deliberately —
see the open question below.

### Flags — lazy evaluation is viable

`PUSHFD` appears **3 times** and `POPFD` **2 times**, in the whole binary.

Materialising EFLAGS after every arithmetic op is the naive approach and it is slow.
Because only these 5 sites ever observe the whole flags register, we can evaluate flags
lazily (keep operands + last-op kind, compute the flag on demand) everywhere else, and
only fully materialise at those 5. `SETcc` (~60 sites) and `Jcc` (~9000) each read one
or two specific flags, which lazy evaluation handles directly.

### Other one-offs

| | Count | Note |
|---|---|---|
| `CPUID` | 2 | stub with fixed values |
| `MOVAPD`, `CVTTSD2SI` | 1 each | the only SSE2; CRT double→int |
| `PUSHFD`/`POPFD` | 5 | see above |
| `XCHG` | 1 | |
| String ops (`CMPSB.REPE` 216, `MOVSD.REP` 30, `MOVSB.REP` 11, `STOSD.REP` 10, …) | ~285 | `REP` prefixes fold into the instruction; these are inlined `memcmp`/`memcpy`/`memset` |
| `DIV`/`IDIV` | 141 | needs correct #DE semantics |

No privileged instructions, no self-modifying-code indicators, no `INT` beyond CRT
scaffolding.

## Decided — x87 precision: use host `double`

Measured rather than assumed. Every x87 instruction was classified by its memory-operand
width (escape byte + ModRM `reg` field):

| Operand | Count |
|---|---|
| `m64fp` (double) — `FSTP` 554, `FLD` 468, arith 299, `FST` 109 | **1430** |
| `m32int` (`FILD`) | 186 |
| `m32fp` (float) | **5** |
| `m80fp` (extended) | **0** |
| register-only forms (`FXCH`, `FLDZ`, `FCHS`, `FADDP` …) | 650 |

Three facts settle it:

1. **The game's own storage format is already `double`.** 1430 of 1623 memory operands are
   `m64fp`; `m32fp` appears 5 times in the whole binary.
2. **No value at 80-bit precision ever reaches memory** — zero `m80fp` loads or stores. So
   no *observable* game state carries extended precision; 80 bits exists only transiently
   inside the FPU register stack, and every store rounds to `double`.
3. **No transcendentals.** The full x87 set is `FADD FADDP FCHS FCOM FCOMP FCOMPP FDIV
   FDIVP FDIVR FDIVRP FILD FISTP FLD FLD1 FLDZ FMUL FMULP FNSTSW FST FSTP FSUB FSUBP
   FSUBR FSUBRP FXCH` — arithmetic, compares and moves only. There is no `FSQRT`, `FSIN`,
   `FCOS`, `FPATAN` or `F2XM1`, which is where x87-vs-libm divergence usually bites. That
   entire class of bug cannot occur here.

Additionally there is **no `FLDCW`/`FNSTCW`/`FNINIT` anywhere in `.text`** — the game never
touches the x87 control word, so precision control is whatever `MSVCR80`'s startup leaves
it at.

**This also retires the `long double` option outright.** Since the game stores `double`,
matching x87 exactly would need true 80-bit semantics — and `long double` is 128-bit quad
on ARM64, which is *more* precise than x87 and diverges just as surely. It is not the
portable-faithful choice anywhere except x86-64.

### Residual risk, and what would falsify this

Divergence remains possible only *within* a single register-resident chain: a multi-step
expression evaluated at extended precision and rounded once, versus rounded at each step
in `double`. With 1430 memory operands against 650 register-only ops, chains are short.

Two things are **assumed, not proven**:

- that `MSVCR80` initialises x87 precision control to 53-bit (MSVC's documented default,
  unlike glibc which leaves it at 64-bit). If true, every x87 op already rounds to a
  53-bit mantissa and host `double` is **bit-exact**, not merely close. **Not yet verified
  at runtime.**
- that the 15-bit x87 exponent range never matters. Only reachable via intermediate
  overflow/underflow beyond `double`'s range — implausible for 2D fighter coordinates.

**Falsifier:** a differential run against the Wine oracle showing float state diverging.
Check this once the harness can compare state; if it fires, the fallback is software
80-bit for the affected functions only, not globally.

## x87 differential harness — status

The x87 cases in `runtime/test_insn.c` are gated off by default (`LF2_INSN_X87=1` enables
them) because the harness is not yet trustworthy. What is established:

**The plumbing works.** Dumping the `FNSAVE` output (`LF2_X87_DUMP=1`) shows the seeded
control word `0x027f` round-tripping through `FRSTOR` and `FNSAVE`, and the status word
reading `0x1800` — TOP of 3, down from the 4 that was seeded, which proves the `FILD`
under test actually executed and pushed.

**The seeding does not.** The tag word comes back `0x557f`: every register tagged zero or
empty, including the four that were seeded with non-zero doubles converted to `long
double`. So the 80-bit values written into the save area are not being loaded by `FRSTOR`
as intended, and `FILD` therefore reports zero.

That is a much narrower problem than "x87 does not work": the instruction executes, the
state is captured, and only the input register encoding is wrong. Worth resuming from
there rather than from scratch.
