#!/usr/bin/env python3
"""Fill the engine texture cache across a Stage run and prove old entries are reused safely."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "texture_cache_test"
RUN_LOG = OUTPUT / "run.log"

PAD = ",".join(
    (
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
)


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "build/clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    executable = build / "lf2"
    if not executable.is_file():
        print(f"SKIP: {executable} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    OUTPUT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE="1920x1080",
        LF2_MODE="stage",
        LF2_VIRTUAL_PAD=PAD,
        LF2_RENDER_DEBUG="1",
        LF2_ENGINE_DEBUG="1",
        LF2_QUIT_AFTER="2600",
    )
    print("texture cache: filling the engine pool during a long Stage run...", flush=True)
    with RUN_LOG.open("w") as output:
        try:
            result = subprocess.run(
                [str(executable), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print("  FAIL  the Stage run exceeded 300 seconds")
            return 1

    text = RUN_LOG.read_text(errors="replace")
    reports = list(
        re.finditer(
            r"engine textures: (\d+) resident, (\d+) upload\(s\), (\d+) eviction\(s\), "
            r"(\d+) peak live/frame, (\d+) request\(s\) failed",
            text,
        )
    )
    report = reports[-1] if reports else None
    checks = (
        (result.returncode == 0, f"clean exit status ({result.returncode})"),
        ("screens reached" in text and "match@" in text, "the route reached a Stage match"),
        (report is not None, "the engine emitted its texture-cache report"),
        (report is not None and int(report.group(3)) > 0, "the run exceeded lifetime capacity and evicted old sheets"),
        (report is not None and int(report.group(5)) == 0, "no texture request failed"),
        ("art is missing" not in text.lower(), "the renderer reported no missing art"),
    )
    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    if report:
        print(f"        {report.group(0)}")
    print(f"texture cache test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
