#!/usr/bin/env python3
"""Check the running game's background-layer table against the shipped bg.dat files.

The port reads a stage's layer geometry out of the guest heap (runtime/overrides/world.h,
BG_LAYER_*) because widescreen has to know a layer's scroll span and its horizontal repeat.
That address computation is only trustworthy if what it reads is what the DATA says, so this
compares the two independent sources:

  runtime   LF2_BG_TABLE=all, which walks the registry and prints EVERY background record,
            not only the loaded one -- VS mode picked the same stage on six consecutive
            headless runs, so re-running and hoping for a different one is not a test.
  file      tools/re/decrypt_dat.py over every bg.dat under game/bg, decrypted offline with the
            game's own cipher.

Each runtime record is matched to a file by its FULL geometry with no assumed ordering, and
a record that matches nothing is reported against the same-width candidates layer by layer.
Both halves must be non-empty: a run that produced no records, or a game tree with no bg.dat,
exits non-zero saying it compared NOTHING rather than reporting a vacuous pass.

    tools/re/bg_table_check.py <log-with-LF2_BG_TABLE=all-output> [game-dir]
    tools/re/bg_table_check.py --selftest
"""
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DECRYPT = os.path.join(HERE, "decrypt_dat.py")

# A layer is (span, x, y, loop) -- bg.dat's width:, x:, y:, loop:.
# `y` must be anchored: "transparency: 0" contains "y: 0", and matching that made every
# stage's y read as 0 or 1 and every comparison fail for a reason that had nothing to do
# with the table. Anchor every key the same way rather than only the one that bit.
def _field(block, key, default=0):
    m = re.search(r"(?<![A-Za-z0-9_])" + key + r":\s*(-?\d+)", block)
    return int(m.group(1)) if m else default


def parse_files(gamedir):
    """{path: {'width': int, 'layers': [(span,x,y,loop)|None]}}

    A layer entry with no `width:` is a shadow (`bg\\sys\\<stage>\\s.bmp` on its own). It
    still occupies an index in the game's table, so it is kept as None -- dropping it would
    silently shift every later layer and turn an exact check into a wrong one.
    """
    out = {}
    for path in sorted(glob.glob(os.path.join(gamedir, "bg", "*", "*", "bg.dat"))):
        txt = subprocess.run([sys.executable, DECRYPT, path],
                             capture_output=True, text=True).stdout
        m = re.search(r"^width:\s*(\d+)", txt, re.M)
        if not m:
            continue
        layers = []
        for block in re.findall(r"layer:(.*?)layer_end", txt, re.S):
            if not re.search(r"(?<![A-Za-z0-9_])width:", block):
                layers.append(None)
                continue
            layers.append((_field(block, "width"), _field(block, "x"),
                           _field(block, "y"), _field(block, "loop")))
        out[path] = {"width": int(m.group(1)), "layers": layers}
    return out


def parse_runtime(text):
    """{index: {'width': int, 'layers': [(span,x,y,loop)]}} from LF2_BG_TABLE output."""
    out, cur = {}, None
    for line in text.splitlines():
        m = re.match(r"bg table: background (\d+)\s+stage width (\d+)\s+(\d+) layer", line)
        if m:
            cur = int(m.group(1))
            out[cur] = {"width": int(m.group(2)), "layers": []}
            continue
        m = re.match(r"bg table:   layer (\d+)\s+span=(\d+)\s+x=(-?\d+)\s+y=(-?\d+)"
                     r"\s+loop=(\d+)", line)
        if m and cur is not None:
            out[cur]["layers"].append(tuple(int(m.group(i)) for i in (2, 3, 4, 5)))
    return out


def compatible(runtime, filed):
    """The file's layers, with shadow entries taking whatever the runtime has at that index.

    A shadow carries no geometry in the file, so there is nothing to check it against; it is
    still counted so the indices stay aligned.
    """
    if runtime["width"] != filed["width"]:
        return False
    if len(runtime["layers"]) != len(filed["layers"]):
        return False
    return all(f is None or f == r for r, f in zip(runtime["layers"], filed["layers"]))


def report(runtime, files, out=sys.stdout):
    ok = bad = 0
    used = set()
    for idx in sorted(runtime):
        rec = runtime[idx]
        hit = next((p for p, f in sorted(files.items())
                    if p not in used and compatible(rec, f)), None)
        if hit:
            used.add(hit)
            ok += 1
            loops = sum(1 for l in rec["layers"] if l[3])
            print(f"  ok    background {idx:2d} == {os.path.basename(os.path.dirname(hit))}"
                  f"  ({len(rec['layers'])} layers, {loops} looping, width {rec['width']})",
                  file=out)
            continue
        bad += 1
        near = [p for p, f in sorted(files.items()) if f["width"] == rec["width"]]
        print(f"  FAIL  background {idx:2d} (width {rec['width']}, "
              f"{len(rec['layers'])} layers) matches no bg.dat.", file=out)
        if not near:
            print(f"        No bg.dat has width {rec['width']} at all.", file=out)
        for p in near:
            fl = files[p]["layers"]
            print(f"        vs {p} ({len(fl)} layers):", file=out)
            for i in range(max(len(fl), len(rec["layers"]))):
                a = rec["layers"][i] if i < len(rec["layers"]) else None
                b = fl[i] if i < len(fl) else None
                if b is not None and a != b:
                    print(f"          layer {i}: runtime {a}  file {b}", file=out)
    unmatched = sorted(p for p in files if p not in used)
    for p in unmatched:
        print(f"  FAIL  {p} has no runtime record", file=out)
    print(f"\n{ok} of {len(runtime)} runtime records matched a bg.dat exactly, "
          f"{bad} did not, {len(unmatched)} file(s) unmatched", file=out)
    return bad == 0 and not unmatched


SELFTEST_RUNTIME = """\
bg table: background 0  stage width 1500  3 layer(s)
bg table:   layer 0  span=1379   x=0      y=129  loop=0
bg table:   layer 1  span=1500   x=0      y=261  loop=800
bg table:   layer 2  span=1500   x=0      y=296  loop=600
"""


def selftest():
    """Feed the checker one case that MUST pass and one that MUST fail.

    A comparison tool that only ever prints "ok" is indistinguishable from one whose parser
    silently produced two empty sides, which is exactly the bug this file's `y:` anchoring
    comment records. So the negative is asserted, not assumed.
    """
    good = {"x/bc/bg.dat": {"width": 1500, "layers": [
        (1379, 0, 129, 0), (1500, 0, 261, 800), (1500, 0, 296, 600)]}}
    bad = {"x/bc/bg.dat": {"width": 1500, "layers": [
        (1379, 0, 129, 0), (1500, 0, 261, 0), (1500, 0, 296, 600)]}}   # loop wrong
    rt = parse_runtime(SELFTEST_RUNTIME)
    if len(rt) != 1 or len(rt[0]["layers"]) != 3:
        print("selftest FAILED: the runtime parser did not read the sample", file=sys.stderr)
        return 1
    import io
    if not report(rt, good, io.StringIO()):
        print("selftest FAILED: a matching pair was reported as a mismatch", file=sys.stderr)
        return 1
    if report(rt, bad, io.StringIO()):
        print("selftest FAILED: a WRONG loop value was reported as a match -- the checker "
              "cannot see the field it exists to check", file=sys.stderr)
        return 1
    print("selftest ok: the checker accepts a matching table and rejects a wrong loop")
    return 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return selftest()
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    log, gamedir = argv[1], (argv[2] if len(argv) > 2 else os.path.join(HERE, "..", "game"))
    if not os.path.exists(log):
        print(f"no such log: {log} -- compared NOTHING", file=sys.stderr)
        return 2
    runtime = parse_runtime(open(log, encoding="utf-8", errors="replace").read())
    files = parse_files(gamedir)
    if not runtime:
        print(f"{log} holds no 'bg table: background' lines -- the run did not have "
              f"LF2_BG_TABLE=all set, or never reached a match. Compared NOTHING.",
              file=sys.stderr)
        return 2
    if not files:
        print(f"no bg.dat found under {gamedir}/bg -- compared NOTHING.", file=sys.stderr)
        return 2
    print(f"comparing {len(runtime)} runtime record(s) against {len(files)} bg.dat file(s)")
    return 0 if report(runtime, files) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
