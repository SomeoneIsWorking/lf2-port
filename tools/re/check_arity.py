#!/usr/bin/env python3
"""Cross-check import arities against what the game actually pushes.

Every bug found so far in the runtime has been the same shape: a host handler popping
the wrong number of stdcall arguments. The callee pops, so a mismatch silently shifts
the guest stack and surfaces much later as a wild pointer.

The game's own call sites are the ground truth. For each `CALL dword ptr [IAT slot]`
this walks backwards through the basic block counting PUSH instructions, and reports
the count per import. Compare that against the runtime's ret_stdcall(N, ...).

Counting pushes is a heuristic -- arguments can be written with MOV rather than pushed,
and a block can set up two calls -- so treat a disagreement as something to check by
hand against the documented signature, not as proof.

Usage: check_arity.py <lf2.exe> <instructions.tsv>
"""

import collections
import re
import struct
import sys

STOP = ("CALL", "RET", "JMP", "LEAVE")


def imports_by_iat(path):
    """Map IAT virtual address -> "DLL.Name"."""
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sect = pe + 24 + optsz

    spans = []
    for i in range(nsec):
        s = data[sect + i * 40:sect + (i + 1) * 40]
        vsize, rva, rsize, roff = struct.unpack_from("<IIII", s, 8)
        spans.append((rva, max(vsize, rsize), roff))

    def off(rva):
        for start, size, roff in spans:
            if start <= rva < start + size:
                return roff + (rva - start)
        return None

    def cstr(o):
        return data[o:data.index(b"\0", o)].decode("latin1")

    table = {}
    p = off(struct.unpack_from("<I", data, pe + 24 + 104)[0])
    while True:
        oft, _, _, name_rva, fta = struct.unpack_from("<IIIII", data, p)
        if not name_rva:
            break
        dll = cstr(off(name_rva))
        thunk = off(oft or fta)
        slot = 0
        while True:
            v = struct.unpack_from("<I", data, thunk + slot * 4)[0]
            if not v:
                break
            name = f"#{v & 0xffff}" if v & 0x80000000 else cstr(off(v) + 2)
            table[base + fta + slot * 4] = f"{dll}.{name}"
            slot += 1
        p += 20
    return table


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    iat = imports_by_iat(sys.argv[1])

    insns = []
    for line in open(sys.argv[2]):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 5:
            insns.append((int(f[0], 16), f[2], f[4]))
    insns.sort()

    call_indirect = re.compile(r"^CALL dword ptr \[0x([0-9a-fA-F]+)\]$")
    counts = collections.defaultdict(collections.Counter)

    for i, (_, mnemonic, text) in enumerate(insns):
        m = call_indirect.match(text)
        if not m:
            continue
        name = iat.get(int(m.group(1), 16))
        if not name:
            continue
        pushes = 0
        for j in range(i - 1, max(-1, i - 40), -1):
            prev_mn, prev_txt = insns[j][1], insns[j][2]
            if prev_mn.startswith(STOP):
                break
            if prev_mn == "PUSH":
                pushes += 1
            elif prev_mn.startswith("J"):
                break
        counts[name][pushes] += 1

    print(f"{'import':44} {'sites':>5}  push counts observed")
    for name in sorted(counts):
        seen = counts[name]
        total = sum(seen.values())
        summary = ", ".join(f"{n}x{c}" for n, c in sorted(seen.items(), reverse=True))
        flag = "  <-- inconsistent" if len(seen) > 1 else ""
        print(f"{name:44} {total:5}  {summary}{flag}")


if __name__ == "__main__":
    main()
