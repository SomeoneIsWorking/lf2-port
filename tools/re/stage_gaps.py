#!/usr/bin/env python3
"""Which columns of a widescreen view each stage has no picture for (issues #23, #62).

The work order for hand-weaving a stage. LF2's backgrounds are flat layers and the BACKMOST
one is the only thing behind everything else, so the columns IT does not reach are the only
genuinely empty ones -- black on screen, with nothing to draw over them.

WHY THIS EXISTS RATHER THAN A NOTE IN AN ISSUE. The obvious predicate, "a non-looping layer
whose span is smaller than the view", is wrong and this says so out loud: it catches CUHK's
180-pixel patch of grass and Queen's Island's 127-pixel lamp post, which are PROPS with the
next layer behind them. Run with --all to see that list and why it is not the answer.

TWO NUMBERS THAT ARE NOT THE SAME, and the whole measurement turns on it:

  bg.dat `width:`   the layer's SPAN -- how far it scrolls, not how wide its picture is. The
                    parallax is -(span - 794) * camera / (stage_width - 794), so `span - 794`
                    IS the scroll range: a layer whose span equals its picture width does not
                    move at all.
  the BMP header    the picture's real width, which bg.dat never states. Read from the file.

A layer covers columns [x, x + picture_width). The backmost RUN is the leading layers laid end
to end at the same y -- Brokeback Clif's three cliff pieces, CUHK's doubled floor, the
Templates' pic1+pic2 pair -- so the run's reach is the far edge of the last of them.

    tools/re/stage_gaps.py                     # every stage at a 978 view (a 1080p window)
    tools/re/stage_gaps.py --view 2542         # an ultrawide window
    tools/re/stage_gaps.py --all               # every non-looping layer, with the prop trap
    tools/re/stage_gaps.py --game path/to/game

Exits non-zero if it found no stages at all: a work order over an empty corpus is not "nothing
to author", it is a tool that searched nothing.
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DECRYPT = os.path.join(HERE, "decrypt_dat.py")
SCREEN_W = 794


def bmp_width(game, guest_path):
    """The picture's real width, from the BMP header. None if the file is not there."""
    p = os.path.join(game, guest_path.replace("\\", "/"))
    try:
        with open(p, "rb") as f:
            head = f.read(30)
        if len(head) < 26 or head[:2] != b"BM":
            return None
        return struct.unpack("<i", head[18:22])[0]
    except OSError:
        return None


def parse_layers(text):
    """(path, span, x, y, loop) per layer, in file order. `rect:` blocks are the shadow
    placeholder, not a picture, and are skipped."""
    out = []
    for block in re.findall(r"layer:\s*\n(.*?)layer_end", text, re.S):
        if "rect:" in block:
            continue
        lines = [l.strip() for l in block.strip().splitlines() if l.strip()]
        if not lines:
            continue
        path = lines[0]

        def num(key, default=0):
            m = re.search(r"\b" + key + r":\s*(-?\d+)", block)
            return int(m.group(1)) if m else default

        out.append((path, num("width"), num("x"), num("y"), num("loop")))
    return out


def backmost_run(layers, widths):
    """The leading layers that lie end to end, and how far they reach.

    End to end means the next layer starts exactly where the previous one ended, at the same y
    -- which is how the shipped stages spell a backdrop too wide for one bitmap. The run stops
    at the first layer that does not continue it, because everything after that is drawn OVER
    the backdrop and cannot fill a column the backdrop left black."""
    run = []
    reach = None
    for i, (path, span, x, y, loop) in enumerate(layers):
        w = widths.get(path)
        if w is None:
            break
        if not run:
            if loop > 0:
                return [], None      # a looping backmost layer already fills any view
            run.append(i)
            reach = x + w
            y0 = y
            continue
        if loop > 0 or x != reach or y != y0:
            break
        run.append(i)
        reach = x + w
    return run, reach


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="game", help="the game tree (default: game)")
    ap.add_argument("--view", type=int, default=978,
                    help="composition width in game pixels; 978 is a 1920x1080 window "
                         "(default: 978)")
    ap.add_argument("--all", action="store_true",
                    help="also list every non-looping layer narrower than the view -- the "
                         "PROPS, which are not the answer; see the module docstring")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.game, "bg", "*", "*", "bg.dat")))
    if not files:
        print("stage_gaps: NO bg.dat found under %s/bg/*/*/ -- this searched NOTHING, which is "
              "not the same as 'no stage needs work'. Extract the game tree first "
              "(tools/extract_game.py)." % args.game, file=sys.stderr)
        return 2

    print("stage gaps at a %d-wide view (the game's own screen is %d)\n" % (args.view, SCREEN_W))
    total_short = 0
    for f in files:
        text = subprocess.run([sys.executable, DECRYPT, f],
                              capture_output=True, text=True).stdout
        name = (re.search(r"name:\s*(\S+)", text) or [None, os.path.dirname(f)])[1] \
            if re.search(r"name:\s*(\S+)", text) else os.path.dirname(f)
        stage_w = int((re.search(r"width:\s*(\d+)", text) or [0, 0])[1])
        layers = parse_layers(text)
        widths = {p: bmp_width(args.game, p) for p, _, _, _, _ in layers}

        missing = [p for p, w in widths.items() if w is None]
        run, reach = backmost_run(layers, widths)

        if not layers:
            print("  %-18s NO LAYERS PARSED -- this stage was not measured" % name)
            continue
        if missing:
            print("  %-18s %d layer picture(s) unreadable (%s%s) -- measured WITHOUT them, so "
                  "the reach below is a lower bound"
                  % (name, len(missing), os.path.basename(missing[0]),
                     ", ..." if len(missing) > 1 else ""))
        if reach is None:
            print("  %-18s backmost layer LOOPS -- it already fills any view, nothing to author"
                  % name)
            continue

        pieces = " + ".join(os.path.basename(layers[i][0]) for i in run)
        gap = args.view - reach
        if gap > 0:
            total_short += 1
            print("  %-18s stage %-5d backmost %s reaches %d -> %d COLUMNS TO AUTHOR (%d%% of "
                  "the view)" % (name, stage_w, pieces, reach, gap, 100 * gap // args.view))
        else:
            print("  %-18s stage %-5d backmost %s reaches %d -- covers the view"
                  % (name, stage_w, pieces, reach))

    print("\n%d of %d stage(s) have columns with no picture at a %d-wide view."
          % (total_short, len(files), args.view))
    if total_short == 0:
        print("That is a real negative only if the view is genuinely covered -- re-run with a "
              "wider --view before concluding there is nothing to do.")

    if args.all:
        print("\n---- every non-looping layer narrower than the view, AND WHY THIS IS NOT THE "
              "WORK ORDER ----")
        print("A layer being narrower than the view does not mean a column is empty: almost all "
              "of these\nare PROPS -- a lamp post, a patch of grass, a road end -- with the "
              "backdrop behind them. A fix\ndriven by this list would stretch the lamp posts.\n")
        for f in files:
            text = subprocess.run([sys.executable, DECRYPT, f],
                                  capture_output=True, text=True).stdout
            name = re.search(r"name:\s*(\S+)", text)
            name = name.group(1) if name else os.path.dirname(f)
            short = []
            for path, span, x, y, loop in parse_layers(text):
                if loop > 0:
                    continue
                w = bmp_width(args.game, path)
                if w is not None and x + w < args.view:
                    short.append((x + w, os.path.basename(path), w, x))
            short.sort()
            if short:
                print("  %-18s %2d such layer(s), narrowest %s (%d px at x=%d)"
                      % (name, len(short), short[0][1], short[0][2], short[0][3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
