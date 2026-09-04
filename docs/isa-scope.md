# LF2 x86 JIT scope

This document preserves title-specific instruction evidence that constrains
`shared/x86port`. It is not a translator plan. x86 decode, architectural state,
semantics, interpretation, and JIT emission are owned once by `x86port`; LF2
must not keep a title-local decoder or lifter.

## Evidence source and limit

A Ghidra pass over LF2 v2.0a disassembled 70,508 instructions and observed 92
distinct mnemonics. The raw-byte export is `re/instructions.tsv`, which is
gitignored because its byte column can reconstruct game code. Maintainers may
regenerate it from their own executable for an independent decoder corpus, but
it is never a player, build, install, or launch prerequisite.

Ghidra covered 93.6% of `.text`, so the census is a measured lower bound, not
proof that an unobserved opcode does not exist. A reachable block at
`0x004450ec` containing `FNSTCW` was missed by the export and already falsified
the stronger interpretation.

The corpus remains useful for two jobs:

- compare `x86port` decode length and instruction identity with an independent
  disassembler, reporting decoded, disagreed, and refused denominators; and
- prioritize emitter coverage without weakening the rule that a reached
  unsupported instruction must fail by guest PC and bytes.

It must not seed guest function generation or a title-specific static corpus.

## Observed shape

| Top N mnemonics | Observed coverage |
| --- | --- |
| 10 | 81.95% |
| 20 | 94.24% |
| 30 | 97.23% |
| 50 | 99.50% |
| 60 | 99.81% |

`MOV` accounts for 24,303 observed instructions (34%). The next nine are
`PUSH`, `CMP`, `ADD`, `JNZ`, `CALL`, `LEA`, `JZ`, `POP`, and `JMP`. No observed
LF2 code uses MMX, and only one `MOVAPD` and one `CVTTSD2SI` appeared in the
corpus. These counts guide coverage; they do not authorize an instruction to be
omitted from the runtime.

Other observed pressure points:

| Instruction class | Observed count or fact | JIT implication |
| --- | --- | --- |
| x87 | 2,273 instructions, 25 mnemonics | Full stack, status/control-word, exception, and rounding state belongs in the canonical CPU contract |
| Conditional branches | roughly 9,000 | Flag production/consumption needs block-level differential coverage |
| `PUSHFD` / `POPFD` | 3 / 2 | Complete visible EFLAGS state must round-trip |
| String operations | roughly 285 | Direction flag, REP termination, faults, and partial progress are architectural |
| `DIV` / `IDIV` | 141 | Divide error must be delivered as a named guest exception |
| `CPUID` | 2 | Result is title-visible platform policy, not a guessed host passthrough |

## x87 evidence

The Ghidra corpus observed 1,430 `m64fp` operands, 186 `m32int` operands, five
`m32fp` operands, no `m80fp` memory operand, and 650 register-only forms. That
shows LF2 stores most observed floating-point state as double precision, but it
does not prove transient 80-bit precision is irrelevant or that the unseen
6.4% contains no other form.

The prior title-local implementation used host `double` and found real operand-
direction and save-image bugs through hardware differential testing. Preserve
the facts, not that implementation:

- `DC`/`DE` register forms of `FSUB`/`FDIV` reverse different operands than the
  corresponding `D8` forms;
- an FSAVE image's value slots are in logical stack order while the tag word is
  indexed by physical register; and
- the reachable CRT reads an x87 control word and expects masked exceptions.

`x86port` is now the semantic owner. Its JIT and test interpreter must agree on
the full x87 state, and the independent Wine/hardware leg decides title
faithfulness. LF2 must not retain a second `double`-based x87 authority after
migration.

## Flag evidence

The retired title-local differential exposed an ADC/SBB carry bug and an
unsequenced FPU pop/store bug. The durable lesson is that every input consumed
by the semantic model must vary, undefined-but-observable flags must be compared
against the chosen oracle, and a negative control must demonstrably fail.

The product uses `x86port`'s one flag model. LF2-specific tests may feed real
title encodings into that production model, but they may not duplicate flag
formulas or retain the old decoder/lifter as an oracle.

## First title discriminator

The first bounded LF2 JIT test executes guest `0x004031b0` from the
authenticated image with `ECX=0x00458440` and return sentinel `0x004462e0`.
`docs/migration.md` defines the complete state comparison and controlled
negative. The interpreter is linked only into that separately built test target
(diagnostic tests included). Build-graph, link-map/symbol, and selector
inspection must prove the gameplay product contains no interpreter execution,
interpreter-backed helpers, or fallback machinery; a zero observed-fallback
counter is only supplementary telemetry.

## Coverage and refusal reporting

For every LF2 diagnostic, report at minimum:

- bytes/blocks offered to decode and how many decoded or refused;
- distinct instructions translated, block entries, cache hits, and
  invalidations;
- instructions implemented as native JIT emission versus interpreter-backed
  helpers; the gameplay answer for the latter is zero;
- the exact first unsupported guest PC and bytes; and
- test-interpreter blocks checked, skipped, and divergent in diagnostic builds.

A clean run with zero scanned blocks, an omitted corpus, or an unreported skip
is not evidence.
