#!/usr/bin/env python3
"""Prove mapped controller navigation reaches the RmlUi controls page."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "settings_test"
RUN_LOG = OUTPUT / "run.log"


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "build/clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    if not (build / "lf2").is_file():
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    output = OUTPUT
    output.mkdir(parents=True, exist_ok=True)
    pad = (
        "start@modemenu+60,down@modemenu+100,down@modemenu+140,"
        "down@modemenu+180,down@modemenu+220,south@modemenu+260"
    )
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        # Attach four virtual pads but drive only the fourth. This is the negative for an UI
        # translator that still hardcodes the first one or two controller slots.
        LF2_VIRTUAL_PAD="",
        LF2_VIRTUAL_PAD2="",
        LF2_VIRTUAL_PAD3="",
        LF2_VIRTUAL_PAD4=pad,
        LF2_RMLUI_DEBUG="1",
        LF2_QUIT_AFTER="1700",
    )
    print("RmlUi settings: mapped controller navigation to the controls page...")
    with RUN_LOG.open("w") as log:
        try:
            subprocess.run(
                [str(build / "lf2"), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print("  FAIL  the game exceeded the route's 300-second wall clock")
            return 1

    text = payload_text(RUN_LOG.read_text(errors="replace"))
    report = next(
        (line for line in reversed(text.splitlines()) if line.startswith("rmlui: ")),
        "",
    )
    checks = (
        (
            "controller 3 connected: lf2 virtual pad\n" in text,
            "all four controller slots were live",
            "the fourth controller never bound, so this run cannot test it",
        ),
        (
            "LF2_VIRTUAL_PAD4: 6 of 6 items fired\n" in text,
            "only controller slot four carried the navigation script",
            "the fourth controller did not fire every scripted action",
        ),
        (
            text.count("rmlui input: controller down\n") >= 4,
            "mapped navigation from controller slot four reached RmlUi four times",
            "controller slot four did not deliver all mapped navigation presses",
        ),
        (
            "rmlui input: controller attack\n" in text
            and "rmlui page: controls\n" in text,
            "mapped controller Confirm activated the Controls tab",
            "controller Confirm did not activate the Controls tab",
        ),
        (
            "1 settings open(s)" in report and "rendered frame(s)" in report,
            "the global RmlUi shell opened and rendered",
            "the RmlUi shutdown report did not prove one rendered opening",
        ),
        (
            "2 shared SVG device texture(s) loaded" in report,
            "keyboard and gamepad SVG textures came from shared embedded assets",
            "both shared device textures were not loaded",
        ),
    )
    failed = False
    for passed, success, failure in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {success if passed else failure}")
        failed |= not passed
    print(f"RmlUi settings test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
