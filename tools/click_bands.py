#!/usr/bin/env python3
"""Recover the clickable bands of every mouse-driven screen.

LF2's menus are hit-tested against two globals -- mouse x at 0x4546f0 and mouse y at
0x453cdc -- compared against literal bounds. Estimating button positions from a screenshot
does not work (an early attempt clicked above the first band entirely and nothing
happened); the game's own comparison constants are the answer, so this reads them out of
the generated C.

Two things this gets right that a naive version does not:

  * Only comparisons against the register the coordinate was loaded into are hit tests.
    Taking every literal in a window sweeps up unrelated arithmetic and buries the bands.
  * Bounds are reported in SOURCE ORDER, never sorted. A band is a consecutive
    (x_lo, x_hi, y_lo, y_hi); sorting destroys the pairing and invents bands that do not
    exist.

Functions that read the mouse but yielded no bands are listed explicitly, because a short
table reads as "this is all of them" when it may mean "the parser did not match".
"""
import re, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'scratch/gen_after.c'
AXIS = {'0x4546f0u': 'x', '0x453cdcu': 'y'}
DEF  = re.compile(r'^(?:static )?void (fn_[0-9a-f]+)\(void\)\s*$')
LOAD = re.compile(r'R\((E[A-Z]{2})\) = LD32\((0x4546f0u|0x453cdcu)\)')
CMP  = re.compile(r'_a=R\((E[A-Z]{2})\), _b=(0x[0-9a-f]+)u')

src = open(SRC).read().split('\n')
defs = [(i, m.group(1)) for i, l in enumerate(src) if (m := DEF.match(l))]
spans = [(name, a, defs[k+1][0] if k+1 < len(defs) else len(src))
         for k, (a, name) in enumerate(defs)]

def tests(a, b):
    """Ordered (axis, value) comparisons against a loaded mouse coordinate."""
    held, out = {}, []
    for i in range(a, b):
        m = LOAD.search(src[i])
        if m:
            held[m.group(1)] = AXIS[m.group(2)]
            continue
        for r, c in CMP.findall(src[i]):
            if r in held:
                v = int(c, 16)
                if v < 0x10000:
                    out.append((held[r], v))
    return out

def bands(seq):
    """An x pair followed by one or more y pairs.

    The x range is tested once and then y is compared against each entry's range in turn,
    so a menu of three items is x,x,y,y,y,y,y,y. Requiring a strict x,x,y,y quadruple finds
    only the first entry of every menu and silently loses the rest.
    """
    out, i = [], 0
    while i < len(seq) - 3:
        a, b = seq[i], seq[i + 1]
        if (a[0], b[0]) != ('x', 'x') or a[1] >= b[1]:
            i += 1
            continue
        j = i + 2
        while j < len(seq) - 1 and seq[j][0] == 'y' and seq[j + 1][0] == 'y' \
              and seq[j][1] < seq[j + 1][1]:
            out.append((a[1], b[1], seq[j][1], seq[j + 1][1]))
            j += 2
        i = j if j > i + 2 else i + 1
    return out

reads, found = [], 0
for name, a, b in spans:
    if not any('0x4546f0u' in src[i] or '0x453cdcu' in src[i] for i in range(a, b)):
        continue
    reads.append(name)
    bs = bands(tests(a, b))
    if not bs:
        continue
    found += 1
    print(name)
    pts = []
    for (x0, x1, y0, y1) in bs:
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        print(f"   x {x0:>4}..{x1:<4} y {y0:>4}..{y1:<4}  -> click {cx},{cy}")
        pts.append(f"{cx},{cy}")
    print(f"   LF2_AUTOCLICK={';'.join(pts)}\n")

print(f"{len(reads)} functions read the mouse globals; {found} yielded bands")
silent = [n for n in reads if n not in ()]
if found < len(reads):
    print("no bands recovered from: " +
          ' '.join(n for n in reads if n) if found == 0 else
          "(functions above are the ones that parsed; the rest had no x,x,y,y run)")
