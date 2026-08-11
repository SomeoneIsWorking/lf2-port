#!/usr/bin/env python3
"""Classify every x87 instruction by memory-operand width.

This is the measurement behind the "use host double" decision in docs/isa-scope.md.
An x87 memory operand's width is not implied by the mnemonic -- FLD/FSTP load and store
float, double or 80-bit extended depending on the escape byte (D8..DF) and the ModRM
`reg` field. Rerun this after any re-dump to confirm the decision still holds.

Usage: x87_profile.py [re/instructions.tsv]
"""

import collections
import sys

PREFIXES = {0x66, 0x67, 0xF2, 0xF3, 0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65}

# (escape byte, ModRM reg) -> operand description. A `None` reg means the width is
# fixed by the escape byte alone.
WIDTH = {
    (0xD9, 0): "m32fp FLD",   (0xD9, 2): "m32fp FST",   (0xD9, 3): "m32fp FSTP",
    (0xDD, 0): "m64fp FLD",   (0xDD, 2): "m64fp FST",   (0xDD, 3): "m64fp FSTP",
    (0xDB, 5): "m80fp FLD",   (0xDB, 7): "m80fp FSTP",
    (0xDB, 0): "m32int FILD", (0xDB, 2): "m32int FIST", (0xDB, 3): "m32int FISTP",
    (0xDF, 0): "m16int FILD", (0xDF, 3): "m16int FISTP",
    (0xDF, 5): "m64int FILD", (0xDF, 7): "m64int FISTP",
    (0xD8, None): "m32fp arith",  (0xDC, None): "m64fp arith",
    (0xDA, None): "m32int arith", (0xDE, None): "m16int arith",
}

CONTROL_WORD = {"FLDCW", "FNSTCW", "FSTCW", "FNINIT", "FINIT"}
TRANSCENDENTAL = {"FSQRT", "FSIN", "FCOS", "FPTAN", "FPATAN", "F2XM1", "FYL2X", "FSCALE"}


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "re/instructions.tsv"
    widths = collections.Counter()
    mnemonics = set()
    register_only = total = 0
    control = transcendental = 0

    for line in open(path):
        fields = line.rstrip("\n").split("\t")
        if len(fields) < 4:
            continue
        mnemonic, hexbytes = fields[2], fields[3]
        if not mnemonic.startswith("F"):
            continue

        total += 1
        mnemonics.add(mnemonic)
        control += mnemonic in CONTROL_WORD
        transcendental += mnemonic in TRANSCENDENTAL

        raw = bytes.fromhex(hexbytes)
        i = 0
        while i < len(raw) and raw[i] in PREFIXES:
            i += 1
        if i + 1 >= len(raw) or not 0xD8 <= raw[i] <= 0xDF:
            continue

        escape, modrm = raw[i], raw[i + 1]
        if modrm >= 0xC0:           # register-register form, no memory operand
            register_only += 1
            continue
        reg = (modrm >> 3) & 7
        widths[WIDTH.get((escape, reg)) or WIDTH.get((escape, None))
               or f"esc{escape:02X}/{reg}"] += 1

    print(f"x87 instructions:   {total}")
    print(f"  register-only:    {register_only}")
    print(f"  memory-operand:   {sum(widths.values())}")
    for name, count in widths.most_common():
        print(f"    {count:5}  {name}")
    print(f"\ncontrol-word instructions: {control}   transcendentals: {transcendental}")
    print(f"distinct mnemonics ({len(mnemonics)}): {' '.join(sorted(mnemonics))}")

    extended = sum(c for n, c in widths.items() if "m80fp" in n)
    print(f"\n80-bit values reaching memory: {extended}"
          f"  -> host double is {'SAFE' if extended == 0 else 'NOT obviously safe'}")


if __name__ == "__main__":
    main()
