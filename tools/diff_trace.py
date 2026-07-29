#!/usr/bin/env python3
"""Compare the port's DirectDraw call sequence against the Wine oracle's.

Chasing a wrong pointer backwards through a 70k-instruction program does not converge.
This asks the answerable question instead: at which call does our run first stop
matching a real one?

Both sides are reduced to a sequence of Interface::Method names and compared. Wine's
ddraw channel names its internal functions after the COM methods they implement
(ddraw_surface1_Blt, ddraw1_CreateSurface, ...), so the two vocabularies map cleanly.

    WINEDEBUG=+ddraw wine lf2.exe            2>oracle.log
    LF2_COM_TRACE=1 ./lf2 lf2.exe            2>mine.log

Usage: diff_trace.py <oracle.log> <mine.log>
"""

import re
import sys

# Wine internals that are not COM methods the game called.
INTERNAL = re.compile(
    r"^(DDRAW_|DSOUND_|wined3d|device_parent|ddraw_surface_create|ddraw_texture_init|"
    r"ddraw_surface_update_frontbuffer|ddrawformat|wined3dformat|secondarybuffer|"
    r"primarybuffer|mixieee|setup_dsound|send_device|enumerate_)")

# The game uses the DirectDraw 1 interfaces. Wine implements those by forwarding to its
# version 7 objects, so a single game call shows up as ddraw1_X then ddraw7_X then an
# internal helper. Matching only the version-1 entries gives one line per real call.
IFACE = [
    (re.compile(r"^ddraw_surface1_(.+)$"), "IDirectDrawSurface"),
    (re.compile(r"^ddraw_clipper_(.+)$"),  "IDirectDrawClipper"),
    (re.compile(r"^ddraw_palette_(.+)$"),  "IDirectDrawPalette"),
    (re.compile(r"^ddraw1_(.+)$"),         "IDirectDraw"),
]

RENAME = {}


def from_oracle(path):
    seq = []
    line_re = re.compile(r":trace:ddraw:([A-Za-z_0-9]+)")
    for line in open(path, errors="replace"):
        m = line_re.search(line)
        if not m:
            continue
        fn = m.group(1)
        if fn in RENAME:
            if RENAME[fn]:
                seq.append(RENAME[fn])
            continue
        if INTERNAL.match(fn):
            continue
        for pat, iface in IFACE:
            got = pat.match(fn)
            if got:
                seq.append(f"{iface}::{got.group(1)}")
                break
    return seq


# The oracle log comes from Wine's ddraw channel, so it can only ever contain
# DirectDraw calls. Compare like with like.
DDRAW_IFACES = ("IDirectDraw::", "IDirectDrawSurface::", "IDirectDrawClipper::",
                "IDirectDrawPalette::")


def from_mine(path):
    seq = []
    for line in open(path, errors="replace"):
        if not line.startswith("TRACE "):
            continue
        call = line.split(None, 1)[1].strip()
        if call.startswith(DDRAW_IFACES):
            seq.append(call)
    return seq


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    oracle, mine = from_oracle(sys.argv[1]), from_mine(sys.argv[2])
    print(f"oracle: {len(oracle)} calls    port: {len(mine)} calls")

    # Wine's DirectDrawCreate does its own QueryInterface/AddRef/Initialize dance before
    # the game gets the object, so align both sides on the port's first call.
    if mine:
        try:
            skip = oracle.index(mine[0])
        except ValueError:
            skip = 0
        if skip:
            print(f"(skipping {skip} oracle calls made inside DirectDrawCreate)")
        oracle = oracle[skip:]
    print()

    # Wine's own implementation makes COM calls the game never made (SetClipper does an
    # AddRef and a GetHWnd, for instance), so the oracle legitimately has extra entries.
    # The port's sequence must therefore be a SUBSEQUENCE of the oracle's, not equal to
    # it: every call we make must appear, in order, within a small window.
    WINDOW = 40
    oi = 0
    for mi, call in enumerate(mine):
        found = -1
        for k in range(oi, min(oi + WINDOW, len(oracle))):
            if oracle[k] == call:
                found = k
                break
        if found < 0:
            print(f"divergence at port call {mi}: {call}")
            print("  not found in the oracle within the next "
                  f"{WINDOW} calls from oracle index {oi}.\n")
            print("  port, leading up to it:")
            for j in range(max(0, mi - 6), mi + 1):
                print(f"    {j:5}  {mine[j]}")
            print("\n  oracle, from where we are:")
            for j in range(oi, min(oi + 10, len(oracle))):
                print(f"    {j:5}  {oracle[j]}")
            return 1
        oi = found + 1

    print(f"all {len(mine)} port calls matched in order.")
    print(f"the oracle continues for {len(oracle) - oi} more calls; the port stopped.")
    print("\n  oracle, next:")
    for j in range(oi, min(oi + 12, len(oracle))):
        print(f"    {j:5}  {oracle[j]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
