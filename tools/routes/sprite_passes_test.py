#!/usr/bin/env python3
"""Prove the object-sprite sampling chain does what it says on a real match frame (issue #112).

Four arms of the SAME match, differing only in ``LF2_SPRITE_PASSES``:

``base``      no chain at all -- the picture this engine has always drawn.
``identity``  ``nearest:2``. Magnifying the art by an integer and then sampling it on an
              integer grid selects the SAME texel for every fragment, so this frame must be
              BYTE-IDENTICAL to ``base``. That is the discriminator: it fails the moment the
              chain's coordinate walk drifts by a texel, and it caught exactly that -- taps
              addressed from the frame's uv origin instead of from a whole sheet texel
              resampled one sprite of the two while leaving the other alone.
``coarse``    ``nearest:1/2``. Half the resolution must CHANGE the picture; a chain that
              silently did nothing would pass the identity arm and fail here.
``outline``   ``outline:1``. Must change the picture AND must paint pixels darker than
              anything the base frame had at those positions -- an outline that is merely
              "different" could be any bug at all.

Every arm reports its numbers whether it passes or fails: a route that prints nothing on the
negative cannot be told apart from one that never looked.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

from ppm import read_ppm

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "sprite_passes_test"
FRAME = "@match+282"
PAD_ACTIONS = (
    "south@modemenu+60",
    "south@charselect+58",
    "south@charselect+118",
    "south@charselect+178",
    "south@charselect+238",
    "up@charselect+298",
    "up@charselect+358",
    "south@charselect+418",
    "south@charselect+618",
    "south@charselect+838",
    "up@overlay+99",
    "up@overlay+159",
    "south@overlay+219",
)
ARMS = (
    ("base", ""),
    ("identity", "nearest:2"),
    ("coarse", "nearest:1/2"),
    ("outline", "outline:1"),
)


def arm_environment(case: Path, passes: str) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items() if not key.startswith("LF2_")}
    env.update(
        SDL_AUDIODRIVER="dummy",
        SDL_VIDEODRIVER="offscreen",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_ENGINE="1",
        # The lighting is off in every arm: this route is about SAMPLING, and shading the
        # frame as well would leave two reasons for a difference and no way to tell them apart.
        LF2_HD2D="off",
        LF2_VIRTUAL_PAD=",".join(PAD_ACTIONS),
        LF2_FRAME_DUMP=FRAME,
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1460",
    )
    if passes:
        env["LF2_SPRITE_PASSES"] = passes
    return env


def run_arm(build: Path, game: Path, name: str, passes: str) -> tuple[Path, str] | None:
    case = OUTPUT / name
    case.mkdir(parents=True, exist_ok=True)
    log_path = case / "run.log"
    with log_path.open("w") as log:
        try:
            subprocess.run(
                [str(build / "lf2"), "lf2.exe"],
                cwd=game,
                env=arm_environment(case, passes),
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"  {name}: TIMED OUT after 300 s; see {log_path}")
            return None
    log_text = payload_text(log_path.read_text(errors="replace"))
    if "sprite passes:" in log_text:
        print(f"  {name}: the chain was REFUSED -- {log_text.split('sprite passes:')[1].splitlines()[0].strip()}")
        return None
    frames = sorted(case.glob("frame_*.ppm"))
    if len(frames) != 1:
        print(f"  {name}: {len(frames)} frame(s) dumped, expected 1; the route did not reach the match")
        return None
    return frames[0], log_text


def compare(a: Path, b: Path) -> tuple[int, int, int]:
    """max per-channel difference, differing pixels, total pixels."""
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"{a} is {wa}x{ha} but {b} is {wb}x{hb}")
    worst = differing = 0
    for i in range(0, len(pa), 3):
        delta = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
        if delta:
            differing += 1
            worst = max(worst, delta)
    return worst, differing, wa * ha


def darkened(base: Path, other: Path, floor: int = 24) -> int:
    """Pixels the second frame paints near-black where the first had something brighter."""
    _, _, pa = read_ppm(base)
    _, _, pb = read_ppm(other)
    count = 0
    for i in range(0, len(pa), 3):
        if max(pb[i], pb[i + 1], pb[i + 2]) <= floor < max(pa[i], pa[i + 1], pa[i + 2]):
            count += 1
    return count


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    if not (build / "lf2").is_file():
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    print(f"sprite passes: {len(ARMS)} runs of the same match, one frame each ({FRAME})...")
    frames: dict[str, Path] = {}
    for name, passes in ARMS:
        result = run_arm(build, game, name, passes)
        if result is None:
            print(f"sprite passes: FAILED -- the {name} arm produced no frame to compare")
            return 1
        frames[name] = result[0]
        print(f"  {name}: {frames[name].name}  chain={passes or '(none)'}")

    failures: list[str] = []

    worst, differing, total = compare(frames["base"], frames["identity"])
    print(f"  identity vs base: max {worst}, {differing} of {total} pixels differ")
    if differing:
        failures.append("nearest:2 changed the picture; the chain does not reproduce plain nearest")

    worst, differing, total = compare(frames["base"], frames["coarse"])
    print(f"  coarse   vs base: max {worst}, {differing} of {total} pixels differ")
    if differing < 200:
        failures.append(f"nearest:1/2 changed only {differing} pixels; halving the art did not happen")

    worst, differing, total = compare(frames["base"], frames["outline"])
    added = darkened(frames["base"], frames["outline"])
    print(f"  outline  vs base: max {worst}, {differing} of {total} pixels differ, {added} newly near-black")
    if differing < 100:
        failures.append(f"outline:1 changed only {differing} pixels; no border was drawn")
    if added < 100:
        failures.append(f"outline:1 painted only {added} near-black pixels; the border is not the outline")

    if failures:
        print("sprite passes: FAILED")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("sprite passes: ok (identity byte-exact; coarsening and outline both changed the frame)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
