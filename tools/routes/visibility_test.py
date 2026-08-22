#!/usr/bin/env python3
"""Exercise scene visibility through the shipping SDL_GPU renderer and readback path."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "visibility_test"


def run_arm(binary: Path, game: Path, arm: str) -> bool:
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_ENGINE="1",
        LF2_VISIBILITY_PROBE=arm,
        LF2_QUIT_AFTER="20",
    )
    env.pop("LF2_HD2D_SHOW", None)
    if arm == "unlit":
        env["LF2_HD2D"] = "off"
    elif arm.startswith("chars"):
        env["LF2_HD2D"] = "off"
        env["LF2_HD2D_SHOW"] = "chars"
    else:
        env["LF2_HD2D"] = "on"
        # Make mask consumption a large, deterministic colour change while leaving pixels
        # outside the character mask byte-identical to the unlit arm.
        env["LF2_HD2D_KEY"] = "0"
        env["LF2_HD2D_AMBIENT"] = "0.4"
        env["LF2_HD2D_SHADOW"] = "0"

    log = OUTPUT / f"{arm}.log"
    print(f"  {arm}...", flush=True)
    with log.open("w") as output:
        try:
            result = subprocess.run(
                [str(binary), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=120,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print("    FAIL  exceeded 120 seconds")
            return False

    text = log.read_text(errors="replace")
    match = re.search(
        rf"^visibility probe: PASS arm={re.escape(arm)} left=#[0-9a-f]{{6}} right=#[0-9a-f]{{6}}$",
        text,
        re.MULTILINE,
    )
    if result.returncode != 0:
        print(f"    FAIL  game exited with status {result.returncode}; see {log}")
        return False
    if not match:
        report = next(
            (line for line in text.splitlines() if line.startswith("visibility probe:")),
            "visibility probe produced no report",
        )
        print(f"    FAIL  {report}; see {log}")
        return False
    print(f"    {match.group(0)}")
    return True


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    binary = build / "lf2"
    if not binary.is_file():
        print(f"SKIP: {binary} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("visibility: procedural character under an opaque/transparent occluder")
    arms = ("unlit", "chars", "chars-reversed", "lit")
    passed = [run_arm(binary, game, arm) for arm in arms]
    if not all(passed):
        return 1
    print("visibility: PASS -- colour, mask order, transparent depth, and mask consumption")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
