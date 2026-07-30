#!/usr/bin/env python3
"""Diff two LF2_MEM_DUMP snapshots of the game's .data section.

Finds the variable behind an on-screen change: dump before and after a single input, diff,
and filter to what could plausibly be the thing you are looking for. Reading the
disassembly instead means picking one candidate out of hundreds -- a search for a selection
index in the character-select function returned 20+ compares against the right constant,
none of which could be ruled out on sight.

    tools/diff_data.py scratch/frames/data_002300.bin scratch/frames/data_002400.bin
    tools/diff_data.py before.bin after.bin --max 8      # values that stay small

Prints guest addresses, so a hit can go straight into a grep of the generated C.

Reports the denominator either way: how many dwords were compared, how many differed, and
how many survived each filter. "No candidates" from a run that compared nothing looks
exactly like "no candidates" from a run that compared everything, and the two mean opposite
things.
"""
import argparse
import struct
import sys

DATA_BASE = 0x0044D000


def load(path):
    with open(path, "rb") as fh:
        return fh.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--max", type=int, default=None,
                    help="only report dwords whose values are both <= this "
                         "(a selection index is small; a pointer or counter is not)")
    ap.add_argument("--delta", type=int, default=None,
                    help="only report dwords that changed by exactly this much")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=DATA_BASE)
    args = ap.parse_args()

    a, b = load(args.before), load(args.after)
    if len(a) != len(b):
        sys.exit(f"different sizes: {len(a)} vs {len(b)} -- not comparable")
    if not a:
        sys.exit("empty dumps: nothing was compared")

    n = len(a) // 4
    changed = []
    for i in range(n):
        va = struct.unpack_from("<I", a, i * 4)[0]
        vb = struct.unpack_from("<I", b, i * 4)[0]
        if va != vb:
            changed.append((args.base + i * 4, va, vb))

    kept = changed
    if args.max is not None:
        kept = [c for c in kept if c[1] <= args.max and c[2] <= args.max]
    if args.delta is not None:
        kept = [c for c in kept if (c[2] - c[1]) == args.delta]

    for addr, va, vb in kept:
        print(f"{addr:08x}  {va:10d} -> {vb:<10d}  (0x{va:x} -> 0x{vb:x})")

    print(f"\n{n} dwords compared, {len(changed)} differed, {len(kept)} after filters",
          file=sys.stderr)
    if not changed:
        print("NOTHING CHANGED AT ALL between these two dumps. Either the input had no "
              "effect,\nor the frames are on the same side of it.", file=sys.stderr)
    elif not kept:
        print("Every difference was filtered out. Loosen --max/--delta before concluding "
              "the\nvariable is not in .data.", file=sys.stderr)


if __name__ == "__main__":
    main()
