#!/usr/bin/env python3
"""Decrypt an LF2 .dat file (data/*.dat, bg/*/*/bg.dat) to plain text.

The game ships its data encrypted and decrypts it at load time. Reading that data is
otherwise a matter of running the game under a dump switch, which is a poor way to answer a
question like "how wide is this stage's sky layer" -- so this does it offline.

The cipher, from runtime/overrides/assets.c (fn_004148a0), which is proved byte-identical to
the game's own on all 77 files:

    key      "SiuHungIsAGoodBearBecauseHeIsVeryGood", 37 bytes
    header   the first 0x7b = 123 bytes are consumed and discarded, and the key index
             advances once per consumed byte, so the payload starts at key index 123 % 37
    byte     out = (in - key[i]) mod 256, then i = (i + 1) % 37

Line endings are normalised to LF before decryption, as the game's own reader does.

    tools/re/decrypt_dat.py game/bg/sys/cuhk/bg.dat
    tools/re/decrypt_dat.py --layers game/bg/*/*/bg.dat

--layers summarises a background: the stage width, and each layer with its REPEAT PERIOD and
the size of the bitmap that fills it. Those two are not the same thing and the difference is
the whole point -- see docs/issues/0023.
"""
import re
import struct
import sys
import os

KEY = b"SiuHungIsAGoodBearBecauseHeIsVeryGood"
HEADER = 0x7B


def decrypt(raw: bytes) -> bytes:
    buf = raw.replace(b"\r\n", b"\n")
    if len(buf) <= HEADER:
        raise ValueError(f"file is {len(buf)} bytes, shorter than the {HEADER}-byte header "
                         f"-- that is not an encrypted LF2 data file")
    out = bytearray()
    ki = HEADER % len(KEY)
    for b in buf[HEADER:]:
        out.append((b - KEY[ki]) & 0xFF)
        ki = (ki + 1) % len(KEY)
    return bytes(out)


def bmp_size(path):
    """(width, height) of a BMP, or None. Read from the header rather than guessed."""
    try:
        with open(path, "rb") as f:
            d = f.read(26)
        if len(d) < 26 or d[:2] != b"BM":
            return None
        return struct.unpack_from("<i", d, 18)[0], struct.unpack_from("<i", d, 22)[0]
    except OSError:
        return None


LAYER_RE = re.compile(
    r"layer:\s*\n\s*(\S+)\s*\n\s*transparency:\s*(\d+)\s+width:\s*(\d+)\s+"
    r"x:\s*(-?\d+)\s+y:\s*(-?\d+)")


def show_layers(path, text):
    root = path
    for _ in range(4):                      # bg/sys/<name>/bg.dat -> the tree root
        root = os.path.dirname(root)
    name = re.search(r"name:\s*(\S+)", text)
    stage_w = re.search(r"^\s*width:\s*(\d+)", text, re.M)
    print(f"=== {path}   name={name.group(1) if name else '?'}   "
          f"stage width={stage_w.group(1) if stage_w else '?'}")
    hits = 0
    for m in LAYER_RE.finditer(text):
        bmp, _tr, period, x, y = m.groups()
        size = bmp_size(os.path.join(root, bmp.replace("\\", "/")))
        print(f"   {bmp:40} period={period:>5} x={x:>6} y={y:>4} "
              f"bitmap={size[0]}x{size[1]}" if size else
              f"   {bmp:40} period={period:>5} x={x:>6} y={y:>4} bitmap=?")
        hits += 1
    # A background with no layers is a parse failure, not an empty background. Say so rather
    # than printing a clean-looking header and nothing under it.
    if hits == 0:
        print("   NO LAYERS MATCHED -- the file decrypted but nothing parsed, so this says "
              "nothing about the background. Check the layer syntax against the raw text.",
              file=sys.stderr)
        return False
    return True


def main(argv):
    layers = "--layers" in argv
    paths = [a for a in argv[1:] if not a.startswith("-")]
    if not paths:
        print(__doc__, file=sys.stderr)
        return 2
    ok = True
    for p in paths:
        with open(p, "rb") as f:
            text = decrypt(f.read()).decode("latin1")
        if layers:
            ok &= show_layers(p, text)
        else:
            sys.stdout.write(text)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
