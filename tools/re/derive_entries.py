#!/usr/bin/env python3
"""Fill in function entry points Ghidra did not label.

Ghidra only creates a function where it can prove an entry exists, so functions reached
solely through pointer tables (CRT initterm lists, vtables, callbacks) are disassembled
but never marked. Those are exactly the targets an indirect call lands on at runtime.

Every byte of .text that Ghidra disassembled but left outside a function is walked here,
and a synthetic entry is started at the run's first instruction and again after every RET
-- which is where the next function begins.

Usage: derive_entries.py <functions.tsv> <instructions.tsv> <out.tsv>
"""

import sys


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    funcs_path, insns_path, out_path = sys.argv[1:4]

    funcs = []
    for line in open(funcs_path):
        f = line.rstrip("\n").split("\t")
        if len(f) < 3:
            continue
        funcs.append((int(f[0], 16), int(f[1]), f[2]))
    funcs.sort()

    covered = []
    for addr, size, _ in funcs:
        covered.append((addr, addr + size))

    def inside(a: int) -> bool:
        lo, hi = 0, len(covered)
        while lo < hi:
            mid = (lo + hi) // 2
            if a < covered[mid][0]:
                hi = mid
            elif a >= covered[mid][1]:
                lo = mid + 1
            else:
                return True
        return False

    insns = []
    for line in open(insns_path):
        f = line.rstrip("\n").split("\t")
        if len(f) < 3:
            continue
        insns.append((int(f[0], 16), int(f[1]), f[2]))
    insns.sort()

    # Walk the uncovered instructions, starting a synthetic function at each run start
    # and after every RET.
    synthetic = []
    start = None
    prev_end = None
    for addr, length, mnemonic in insns:
        if inside(addr):
            if start is not None:
                synthetic.append((start, prev_end - start))
                start = None
            continue
        if start is None or addr != prev_end:
            if start is not None:
                synthetic.append((start, prev_end - start))
            start = addr
        prev_end = addr + length
        if mnemonic.startswith("RET"):
            synthetic.append((start, prev_end - start))
            start = None
    if start is not None:
        synthetic.append((start, prev_end - start))

    synthetic = [(a, s) for a, s in synthetic if s > 0]
    merged = [(a, s, n) for a, s, n in funcs] + [(a, s, f"sub_{a:08x}") for a, s in synthetic]
    merged.sort()

    with open(out_path, "w") as fh:
        for addr, size, name in merged:
            fh.write(f"{addr:08x}\t{size}\t{name}\n")

    print(f"{len(funcs)} from Ghidra + {len(synthetic)} derived = {len(merged)} entry points")


if __name__ == "__main__":
    main()
