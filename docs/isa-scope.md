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

## Open question — x87 precision strategy

Not yet decided, and it should be decided on evidence rather than by default:

1. **Host `double` (64-bit)** — fast and simple, but diverges from 80-bit intermediates.
2. **Software 80-bit** — faithful, considerably slower.
3. **Host `long double`** — on x86-64 Linux/macOS this *is* 80-bit, but on **ARM64
   (Apple Silicon) it is 128-bit quad**, which is a different result again.

Option 3 looks free but silently changes behaviour on exactly the platform we most want
to support. Measure whether LF2's gameplay is actually sensitive to this before choosing
— the game is a 2D fighter with integer-ish positions, so it may not be, but that must
be shown rather than hoped.
