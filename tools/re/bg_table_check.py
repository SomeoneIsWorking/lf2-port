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

THE NAMES ARE A SECOND, INDEPENDENT CHECK and they are why this file was revisited. The
record also carries bg.dat's `name:` and each layer's bitmap path (world.h BG_STAGE_NAME and
BG_LAYER_NAME, read off fn_0040c160, the parser that writes them). Geometry says "this record
is bc/bg.dat" from four numbers per layer; the strings say the same thing from a completely
different part of the record, written by a different fscanf. Either one alone can be a
coincidence of a wrong stride landing somewhere plausible. Both agreeing on twenty stages
cannot. A name mismatch is a FAILURE, not a note -- a record whose geometry matches one stage
and whose name is another stage's is the exact signature of a stride error.
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
    """{path: {'name': str, 'width': int, 'layers': [...], 'names': [str]}}

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
        nm = re.search(r"^name:\s*(\S+)", txt, re.M)
        layers, names = [], []
        for block in re.findall(r"layer:(.*?)layer_end", txt, re.S):
            # The layer's bitmap path is the block's first token, before any key. Its LEAF is
            # what the runtime reports, because that is what identifies the layer to a human.
            first = next((t for t in block.split() if t), "")
            names.append(re.split(r"[\\/]", first)[-1] if ":" not in first else "")
            if not re.search(r"(?<![A-Za-z0-9_])width:", block):
                layers.append(None)
                continue
            layers.append((_field(block, "width"), _field(block, "x"),
                           _field(block, "y"), _field(block, "loop")))
        out[path] = {"name": nm.group(1) if nm else "", "width": int(m.group(1)),
                     "layers": layers, "names": names}
    return out


def parse_runtime(text):
    """{index: {'name','width','layers','names'}} from LF2_BG_TABLE output."""
    out, cur = {}, None
    for line in text.splitlines():
        m = re.match(r'bg table: background (\d+)\s+"([^"]*)"\s+stage width (\d+)\s+'
                     r"(\d+) layer", line)
        if m:
            cur = int(m.group(1))
            out[cur] = {"name": m.group(2), "width": int(m.group(3)),
                        "layers": [], "names": []}
            continue
        m = re.match(r"bg table:   layer (\d+)\s+span=(\d+)\s+x=(-?\d+)\s+y=(-?\d+)"
                     r"\s+loop=(\d+)\s+(\S*)", line)
        if m and cur is not None:
            out[cur]["layers"].append(tuple(int(m.group(i)) for i in (2, 3, 4, 5)))
            # The record holds the path exactly as bg.dat writes it, backslashes and all --
            # `bg\sys\gw\hill1.bmp` -- and the report prints it unchanged, because what is
            # in memory is what this file exists to check. The LEAF is what identifies the
            # layer, so both sides are reduced to it here rather than in the report.
            out[cur]["names"].append(re.split(r"[\\/]", m.group(6))[-1])
    return out


def same_name(runtime, filed):
    """fn_0040c160 turns every '_' in `name:` into a space as it reads the file, so the record
    says "The Great Wall" where bg.dat says "The_Great_Wall". That substitution is the game's,
    not this port's, so it is undone HERE for the comparison rather than pretended away in the
    report -- the report prints what is actually in memory."""
    return runtime.replace("_", " ").lower() == filed.replace("_", " ").lower()


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
            loops = sum(1 for l in rec["layers"] if l[3])
            # The strings, checked against the file the GEOMETRY chose. Layer names are
            # compared only where the file has one -- a shadow layer's block carries no path
            # of its own -- and the count of names actually compared is printed, so a run in
            # which nothing was comparable cannot read as a run in which everything matched.
            f = files[hit]
            probs = []
            if not same_name(rec["name"], f["name"]):
                probs.append(f'stage name: runtime "{rec["name"]}" file "{f["name"]}"')
            compared = 0
            for i, fname in enumerate(f["names"]):
                if not fname or i >= len(rec["names"]):
                    continue
                compared += 1
                if rec["names"][i].lower() != fname.lower():
                    probs.append(f'layer {i}: runtime "{rec["names"][i]}" '
                                 f'file "{fname}"')
            if probs:
                bad += 1
                print(f"  FAIL  background {idx:2d} has "
                      f"{os.path.basename(os.path.dirname(hit))}'s GEOMETRY but not its "
                      f"NAMES -- the string fields do not agree with the numeric ones:",
                      file=out)
                for pr in probs:
                    print(f"          {pr}", file=out)
                continue
            ok += 1
            print(f"  ok    background {idx:2d} == {os.path.basename(os.path.dirname(hit))}"
                  f'  "{rec["name"]}"  ({len(rec["layers"])} layers, {loops} looping, '
                  f"width {rec['width']}, {compared} layer name(s) matched)", file=out)
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
bg table: background 0  "Brokeback_Clif"  stage width 1500  3 layer(s)
bg table:   layer 0  span=1379   x=0      y=129  loop=0    cliff.bmp
bg table:   layer 1  span=1500   x=0      y=261  loop=800  rock1.bmp
bg table:   layer 2  span=1500   x=0      y=296  loop=600  rock2.bmp
"""


def selftest():
    """Feed the checker one case that MUST pass and one that MUST fail.

    A comparison tool that only ever prints "ok" is indistinguishable from one whose parser
    silently produced two empty sides, which is exactly the bug this file's `y:` anchoring
    comment records. So the negative is asserted, not assumed.
    """
    names = ["cliff.bmp", "rock1.bmp", "rock2.bmp"]
    good = {"x/bc/bg.dat": {"name": "Brokeback_Clif", "width": 1500, "names": names,
                            "layers": [(1379, 0, 129, 0), (1500, 0, 261, 800),
                                       (1500, 0, 296, 600)]}}
    bad = {"x/bc/bg.dat": {"name": "Brokeback_Clif", "width": 1500, "names": names,
                           "layers": [(1379, 0, 129, 0), (1500, 0, 261, 0),
                                      (1500, 0, 296, 600)]}}            # loop wrong
    # Same GEOMETRY, different stage name: the case the string check exists for. A stride
    # error puts a plausible set of numbers in front of the wrong stage's strings, so a
    # checker that reported this as a match would be blind to exactly what it was added for.
    wrongname = {"x/bc/bg.dat": dict(good["x/bc/bg.dat"], name="The_Great_Wall")}
    # The record's spaces against the file's underscores: the SAME stage, and it must pass.
    # Without this, "compare the names" could be satisfied by a check that rejects every
    # stage the game ships, which would be a checker that never goes green rather than one
    # that measures anything.
    spaced = {"x/bc/bg.dat": dict(good["x/bc/bg.dat"], name="Brokeback Clif")}
    wronglayer = {"x/bc/bg.dat": dict(good["x/bc/bg.dat"],
                                      names=["cliff.bmp", "sky.bmp", "rock2.bmp"])}
    rt = parse_runtime(SELFTEST_RUNTIME)
    if len(rt) != 1 or len(rt[0]["layers"]) != 3:
        print("selftest FAILED: the runtime parser did not read the sample", file=sys.stderr)
        return 1
    if rt[0]["name"] != "Brokeback_Clif" or rt[0]["names"] != names:
        print(f"selftest FAILED: the runtime parser read the geometry but not the STRINGS "
              f"-- name {rt[0]['name']!r}, layers {rt[0]['names']!r}", file=sys.stderr)
        return 1
    import io
    if not report(rt, good, io.StringIO()):
        print("selftest FAILED: a matching pair was reported as a mismatch", file=sys.stderr)
        return 1
    if report(rt, bad, io.StringIO()):
        print("selftest FAILED: a WRONG loop value was reported as a match -- the checker "
              "cannot see the field it exists to check", file=sys.stderr)
        return 1
    if report(rt, wrongname, io.StringIO()):
        print("selftest FAILED: matching geometry with the WRONG STAGE NAME was reported as "
              "a match, so the name check is not running", file=sys.stderr)
        return 1
    if not report(rt, spaced, io.StringIO()):
        print("selftest FAILED: the record's \"Brokeback Clif\" was reported as a mismatch "
              "against the file's \"Brokeback_Clif\" -- the underscore substitution "
              "fn_0040c160 does is not being undone", file=sys.stderr)
        return 1
    if report(rt, wronglayer, io.StringIO()):
        print("selftest FAILED: matching geometry with a WRONG LAYER NAME was reported as a "
              "match, so the per-layer name check is not running", file=sys.stderr)
        return 1
    print("selftest ok: the checker accepts a matching table (underscores or spaces) and "
          "rejects a wrong loop, a wrong stage name and a wrong layer name")
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
