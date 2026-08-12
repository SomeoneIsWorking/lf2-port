#!/usr/bin/env python3
"""How bright is each stage's own art? -- offline, over every shipped background layer.

WHY THIS EXISTS. A brightness-thresholded screen effect (a bloom) was written against the port's
G-buffer and glowed nothing at all. The frame-side instrument (LF2_ENGINE_GBUF=1) named the
immediate cause -- on Brokeback Clif, 766 pixels sat above 0.75 luminance and NOT ONE of them
carried a distance, so every bright pixel in the frame was HUD, text or a sprite. What it could
not say is whether that was one dark stage or the whole art set, and answering that by booting
the game once per stage is nine GPU runs for a question the bitmaps answer in two seconds.

WHAT IT FOUND, and it falsifies the approach rather than the tuning:

    stage               >=0.75      >=0.50      max
    bc (Brokeback)       0.000%      0.437%    0.541    <- no threshold >=0.55 selects anything
    gw (Great Wall)     22.602%     41.376%    1.000    <- a threshold there glows a quarter
                                                           of the screen: its sky is genuinely
                                                           near-white art

Both extremes are in the SHIPPED art, so no single absolute threshold behaves as a bloom on
both. Nor does a relative one: gw's top-1% percentile is 1.000, because more than one percent of
its art is exactly white, so a percentile threshold still selects the whole sky. That is the
same defect issue #30 recorded when the first bloom was cut -- a screen-wide effect with nothing
under it to be right about. The depth of field cleared that bar because distance is measured
from bg.dat; luminance is not measured, it is chosen.

THE KEY IS EXCLUDED. LF2's background layers are colour-keyed on pure black and 44-65% of a
typical layer is key rather than art, so counting it would report every stage as much darker
than it draws. Checked the other way too: no layer in the shipped set uses white as its key
(gw/sky.bmp is 3.91% pure white and it is a sky, not a cut-out).

    python3 tools/re/stage_lum.py                 # every stage under game/bg
    python3 tools/re/stage_lum.py --selftest      # prove the decoder and the histogram fire
    python3 tools/re/stage_lum.py --stage gw      # one stage, with its per-layer breakdown

Exits non-zero when it read NOTHING, or when any layer failed to decode -- a scan that silently
drops the files it cannot read reports the brightness of the subset it happened to decode.
"""
import argparse
import glob
import os
import struct
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("stage_lum: needs numpy, and READ NOTHING without it")

# The shadow ellipse the game stamps under an object. Not stage art, and its palette is three
# entries wide, so it is excluded by name rather than allowed to fail the decode.
NOT_ART = ("s.bmp", "shadow.bmp")
KEY = (0, 0, 0)          # LF2 background layers key on pure black
LUM = (0.2126, 0.7152, 0.0722)


def read_bmp(path):
    """8-bit BMP, BI_RGB or BI_RLE8 -> (H, W, 3) uint8 RGB.

    Written out rather than left to Pillow: 43 of the 118 shipped layers are BI_RLE8 and Pillow
    raises `not enough image data` on every one of them. A scan that caught that exception and
    continued would have reported the brightness of the 75 files that happened to decode.
    """
    b = open(path, 'rb').read()
    if b[:2] != b'BM':
        raise ValueError("not a BMP")
    off = struct.unpack_from("<I", b, 10)[0]
    hdr = struct.unpack_from("<I", b, 14)[0]
    w, h, _planes, bpp, comp, _imgsz = struct.unpack_from("<iiHHII", b, 18)
    bottom_up, h = h > 0, abs(h)
    if bpp != 8:
        raise ValueError(f"bpp={bpp}, only 8 is handled")
    npal = struct.unpack_from("<I", b, 46)[0] or 256
    pal = np.frombuffer(b, np.uint8, npal * 4, 14 + hdr).reshape(npal, 4)
    idx = np.zeros((h, w), np.uint8)
    if comp == 0:
        stride = (w * 8 + 31) // 32 * 4
        for y in range(h):
            idx[y] = np.frombuffer(b, np.uint8, w, off + y * stride)
    elif comp == 1:                                   # BI_RLE8
        p, x, y = off, 0, 0
        while p < len(b) - 1:
            n, v = b[p], b[p + 1]
            p += 2
            if n:                                     # n copies of index v
                if y >= h:
                    break
                idx[y, x:min(x + n, w)] = v
                x += n
            elif v == 0:                              # end of line
                x, y = 0, y + 1
            elif v == 1:                              # end of bitmap
                break
            elif v == 2:                              # delta
                x += b[p]
                y += b[p + 1]
                p += 2
            else:                                     # absolute run, word-padded
                if y < h:
                    e = min(x + v, w)
                    idx[y, x:e] = np.frombuffer(b, np.uint8, e - x, p)
                x += v
                p += v + (v & 1)
    else:
        raise ValueError(f"compression={comp}, only BI_RGB and BI_RLE8 are handled")
    if bottom_up:
        idx = idx[::-1]
    if int(idx.max()) >= npal:
        raise ValueError(f"index {int(idx.max())} outside a {npal}-entry palette")
    return pal[idx][:, :, [2, 1, 0]]                  # the palette is BGRA


def layer_hist(path):
    """256-bucket luminance histogram of one layer's LIT pixels, and how many there were."""
    a = read_bmp(path).astype(np.float32)
    lit = ~((a[:, :, 0] == KEY[0]) & (a[:, :, 1] == KEY[1]) & (a[:, :, 2] == KEY[2]))
    l = (LUM[0] * a[:, :, 0] + LUM[1] * a[:, :, 1] + LUM[2] * a[:, :, 2])[lit]
    return np.bincount(np.minimum(l.astype(np.int32), 255), minlength=256), int(l.size)


def tail(hist, t):
    """How many pixels are at or above luminance `t` (0..1)."""
    return int(hist[int(round(t * 255)):].sum())


def scan(root, want=None, per_layer=False):
    rows, bad = [], []
    for d in sorted(glob.glob(os.path.join(root, "*", "*", ""))):
        name = os.path.basename(d.rstrip(os.sep))
        if want and name != want:
            continue
        hist, tot, layers = np.zeros(256, np.int64), 0, []
        for f in sorted(glob.glob(d + "*.bmp")):
            if os.path.basename(f).lower() in NOT_ART:
                continue
            try:
                h, n = layer_hist(f)
            except Exception as e:                    # named, never swallowed
                bad.append((f, repr(e)))
                continue
            hist += h
            tot += n
            layers.append((os.path.basename(f), h, n))
        if tot:
            rows.append((d, hist, tot, layers))
    return rows, bad


def report(rows, bad, per_layer):
    if not rows:
        print("stage_lum: NO stage read at all -- the corpus is missing, not dark", file=sys.stderr)
        return 2
    print(f"{'stage':26} {'lyr':>3} {'lit px':>9}   {'>=.75':>8} {'>=.60':>8} {'>=.50':>8}   {'max':>5}")
    for d, hist, tot, layers in rows:
        mx = max(i for i in range(256) if hist[i]) / 255.0
        print(f"{d:26} {len(layers):3d} {tot:9d}   "
              f"{100*tail(hist,.75)/tot:7.3f}% {100*tail(hist,.60)/tot:7.3f}% "
              f"{100*tail(hist,.50)/tot:7.3f}%   {mx:5.3f}")
        if per_layer:
            for nm, h, n in layers:
                if not n:
                    print(f"    {nm:22} {n:9d}   (entirely key -- no art in it)")
                    continue
                print(f"    {nm:22} {n:9d}   {100*tail(h,.75)/n:7.3f}% "
                      f"{100*tail(h,.60)/n:7.3f}% {100*tail(h,.50)/n:7.3f}%")
    print(f"\n{len(rows)} stage(s), {sum(len(r[3]) for r in rows)} layer(s) read, "
          f"{len(bad)} UNREADABLE")
    for f, e in bad:
        print("  ", f, e)
    return 1 if bad else 0


def selftest():
    """Feed cases that MUST come out a particular way, in both directions.

    A scanner that reports "0.000% above 0.75" is indistinguishable from one that decoded
    nothing, so the negative arm is not enough on its own: every arm here is paired with one
    that must produce the OPPOSITE answer through the same code path.
    """
    ok = True

    def check(what, got, want):
        nonlocal ok
        good = got == want
        ok = ok and good
        print(f"  {'ok  ' if good else 'FAIL'}  {what}: {got!r}" + ("" if good else f" != {want!r}"))

    def bmp8(w, h, pal, idx, rle=False):
        """A minimal 8-bit BMP, uncompressed or RLE8, built here so the decoder is fed a file
        whose right answer is known rather than a shipped one whose answer is the question."""
        palb = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in pal)
        if rle:
            data = b""
            for y in range(h - 1, -1, -1):
                row = idx[y]
                x = 0
                while x < w:
                    n = 1
                    while x + n < w and row[x + n] == row[x] and n < 255:
                        n += 1
                    data += bytes((n, row[x]))
                    x += n
                data += b"\x00\x00"
            data += b"\x00\x01"
            comp = 1
        else:
            stride = (w * 8 + 31) // 32 * 4
            data = b"".join(bytes(idx[y]) + b"\0" * (stride - w) for y in range(h - 1, -1, -1))
            comp = 0
        off = 14 + 40 + len(palb)
        return (b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off)
                + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, comp, len(data), 0, 0, len(pal), 0)
                + palb + data)

    import tempfile
    with tempfile.TemporaryDirectory() as td:
        # Half key-black, half pure white. The KEY must not be counted, so the lit set is 8 px
        # and every one of them is above every threshold.
        idx = [[0, 0, 1, 1] for _ in range(4)]
        for comp, tag in ((False, "BI_RGB"), (True, "BI_RLE8")):
            p = os.path.join(td, f"t_{tag}.bmp")
            open(p, "wb").write(bmp8(4, 4, [(0, 0, 0), (255, 255, 255)], idx, rle=comp))
            h, n = layer_hist(p)
            check(f"{tag}: key excluded, lit pixel count", n, 8)
            check(f"{tag}: all lit pixels above 0.75", tail(h, .75), 8)
            check(f"{tag}: none of them below 0.50 is missing", tail(h, .50), 8)

        # THE OPPOSITE CASE, through the same path: mid-grey art must be counted and must NOT
        # be selected. Without this arm a decoder returning all-black would pass the tests above
        # by reporting an empty lit set, which is the failure this whole file exists because of.
        p = os.path.join(td, "dark.bmp")
        open(p, "wb").write(bmp8(4, 4, [(0, 0, 0), (80, 80, 80)], idx))
        h, n = layer_hist(p)
        check("mid-grey: counted as lit", n, 8)
        check("mid-grey: selected by NO threshold at 0.50", tail(h, .50), 0)
        check("mid-grey: but present below it", tail(h, .10), 8)

        # A missing corpus must REFUSE, not report a clean zero.
        rows, bad = scan(os.path.join(td, "does_not_exist"))
        check("a missing corpus reads nothing", (len(rows), len(bad)), (0, 0))
        check("...and is reported as an error", report(rows, bad, False), 2)

    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default="game/bg", help="where the stage directories live")
    ap.add_argument("--stage", help="just this one, with its per-layer breakdown")
    ap.add_argument("--layers", action="store_true", help="per-layer breakdown for every stage")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not os.path.isdir(a.root):
        print(f"stage_lum: {a.root} does not exist -- SEARCHED NOTHING. The game tree is not in "
              f"this repo; run from the game tree or pass --root", file=sys.stderr)
        return 2
    rows, bad = scan(a.root, a.stage)
    return report(rows, bad, a.layers or bool(a.stage))


if __name__ == "__main__":
    sys.exit(main())
