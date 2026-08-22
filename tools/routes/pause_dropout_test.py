#!/usr/bin/env python3
"""A joined player leaves from its own pad through the live-rendered RmlUi shell."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "pause_dropout_test"
RUN_LOG = OUTPUT / "run.log"

PAD1 = ",".join(
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
PAD2 = "south@match+158,south@match+258,start@match+458,down@match+518,south@match+578"


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def main() -> int:
    build = configured_path("BUILD", "scratch/build-clang")
    game = configured_path("GAME", "game")
    binary = build / "lf2"
    if not binary.is_file():
        print(f"SKIP: {binary} not built")
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
        LF2_VIRTUAL_PAD=PAD1,
        LF2_VIRTUAL_PAD2=PAD2,
        LF2_RENDER_DEBUG="1",
        LF2_QUIT_AFTER="2360",
    )
    print("pause drop-out: a joined player leaves from its own RmlUi menu...", flush=True)
    with RUN_LOG.open("w") as output:
        try:
            result = subprocess.run(
                [str(binary), "lf2.exe"], cwd=game, env=env,
                stdout=output, stderr=subprocess.STDOUT, timeout=300, check=False,
            )
        except subprocess.TimeoutExpired:
            print("  FAIL  the route exceeded 300 seconds")
            return 1

    text = RUN_LOG.read_text(errors="replace")
    checks: list[tuple[bool, str]] = []
    lock = re.search(r"coop select: slot (\d+) LOCKED IN[^\n]*", text)
    if lock:
        slot = lock.group(1)
        checks.append((True, f"pad two locked in at slot {slot}"))
        leave = re.search(
            r"coop leave: slot (\d+) dropped out "
            r"\(the player chose to drop out from the pause menu\)[^\n]*",
            text,
        )
        checks.append((leave is not None, "the pause action dropped out a player"))
        if leave:
            checks.append((leave.group(1) == slot,
                           f"the departing slot {leave.group(1)} is pad two's slot {slot}"))
            checks.append(("gate cleared, devsel -> 0, joined mask" in leave.group(0),
                           "gate, device selector, and joined-mask ownership were all released"))
    else:
        checks.append((False, "pad two joined and locked a fighter before the pause"))

    checks.append(("coop leave: slot 0 dropped out" not in text,
                   "player one remained in the match"))
    checks.append(("LF2_VIRTUAL_PAD: 13 of 13 items fired" in text and
                   "LF2_VIRTUAL_PAD2: 5 of 5 items fired" in text,
                   "all route and per-device pause actions fired"))
    checks.append((result.returncode == 0, f"the game exited cleanly ({result.returncode})"))

    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    print(f"pause drop-out: {'FAILED' if failed else 'ok'}")
    if failed:
        print(f"        full log: {RUN_LOG}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
