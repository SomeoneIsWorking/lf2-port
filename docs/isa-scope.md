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

**Correction: the binary does contain `FNSTCW`.** This section previously claimed there
was none anywhere in `.text`. That was checked by grepping `re/instructions.tsv`, and
Ghidra never disassembled the block at `0x4450ec` that uses it, so the corpus could not
have shown the other answer. The CRT reads the control word back and tests
`(cw & 0x7f) == 0x7f`, i.e. that every exception is masked.

`FLDCW` and `FNSTCW` are now implemented against a `cpu.fcw` initialised to `0x027F`
(MSVC's default: exceptions masked, round-to-nearest, 53-bit precision), so the read-back
sees what it expects. Precision control is stored but not acted on — x87 is evaluated in
host `double`, which is 53-bit regardless. If a future `FLDCW` ever selected 64-bit
precision that would be a real divergence, and the store is there so it can be detected.

The counts in this document come from the same `instructions.tsv` and are therefore lower
bounds. They remain sound for *scoping* — they cannot understate which mnemonics exist in
the disassembled 93.6% — but "the binary contains no X" is not a conclusion this file can
support.

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

## x87 differential harness — working, and it found a real bug

The x87 cases run by default. 8373 encodings x 8 rounds = 66,984 checks against the host
CPU, 0 mismatches.

**Root cause of the long-running harness failure: the FSAVE image is in stack order, not
physical-register order.** Slot *i* is ST(*i*); TOP does not enter the indexing at all.
The harness was indexing slots as physical `R[(TOP + i) & 7]`, which reads correctly only
when nothing pushes. After a push the seeded values appear one slot further along -- old
ST(4) genuinely has become ST(5) -- which looks exactly like a corrupted round-trip and is
not one. `scratch/x87/probe3.c` is the ten-line standalone that pins the convention down.

A second, separate bug: the tag word *is* indexed by physical register, unlike the slots,
so it must be derived from TOP. Hardcoding it marks the wrong registers empty and every
operand then reads as a masked stack underflow returning indefinite QNaN.

### The port bug this uncovered

`FSUB`/`FDIV` at the `DC` and `DE` escapes had their operands reversed. The reverse-form
encoding is *inverted* relative to `D8`: at a `ST(i)` destination, g=4 (`FSUBR`) and g=6
(`FDIVR`) are the reversed ones, whereas at the `ST(0)` destination it is g=5 and g=7. The
lifter used the `D8` rule for all of them, so `FDIVP ST(1),ST(0)` computed the reciprocal
of the correct result and `FSUBP` the negation. Silent, plausible-looking wrong numbers in
game arithmetic.

### Validation

The test is validated the way the integer path is, by a negative control: putting the
reversal back into the lifter makes exactly 24 cases fail, removing it makes them pass. So
the comparison demonstrably detects a wrong answer rather than merely reporting success.
`LF2_X87_NULL=1` drops the instruction under test entirely, leaving a bare `FRSTOR`/
`FNSAVE` round-trip -- that is the control that separates a harness fault from a lifter
fault, and it is what should have been run first.

**Lesson, recorded because it cost four rounds.** This was twice written up as a
confidently-wrong diagnosis -- first "the seeding is broken", then "the round-trip is
broken" -- both from inference over a misread tag word rather than from measurement. Two
cheap moves would have skipped all of it: comparing register *values* in and out instead
of squinting at tag bits, and reproducing the round-trip in a standalone probe with no
harness to blame. The bug was never in the code being tested.
