#!/usr/bin/env python3
"""LEAVE MATCH must be one F4 pulse, with the result overlay left to LF2."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def main() -> int:
    build = configured_path("BUILD", "build/clang")
    game = configured_path("GAME", "game")
    binary = build / "lf2"
    if not binary.is_file():
        print(f"SKIP: {binary} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    pad = [
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
        "start@match+300",
        "down@match+360",
        "south@match+420",
    ]
    env = os.environ.copy()
    env.update(
        {
            "SDL_VIDEODRIVER": "offscreen",
            "SDL_AUDIODRIVER": "dummy",
            "LF2_UNPACED": "1",
            "LF2_VIRTUAL_PAD": ",".join(pad),
            "LF2_QUIT_AFTER": "2400",
        }
    )

    print("leave match: RmlUi action should send F4 once and leave LF2's overlay open...")
    try:
        run = subprocess.run(
            [str(binary), "lf2.exe"],
            cwd=game,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        print("  FAIL  the game did not stop within 300 seconds")
        save_log(output)
        return 1

    output = run.stdout
    save_log(output)
    failed = False

    fired = re.search(r"LF2_VIRTUAL_PAD: (\d+) of (\d+) items fired", output)
    if not fired or fired.group(1) != fired.group(2) or int(fired.group(2)) != len(pad):
        print("  FAIL  the route did not take every menu action, so it proves nothing")
        if fired:
            print(f"        {fired.group(1)} of {fired.group(2)} items fired; expected {len(pad)}")
        failed = True
    else:
        print(f"  ok    route: all {len(pad)} menu actions fired")

    report = re.search(
        r"pause leave: (\d+) F4 pulse\(s\); key (DOWN|released); post-match overlay (up|not up) at shutdown",
        output,
    )
    if not report:
        print("  FAIL  LEAVE MATCH produced no F4/overlay report")
        failed = True
    else:
        pulses, key, overlay = report.groups()
        if pulses == "1":
            print("  ok    action: LEAVE MATCH requested exactly one F4 pulse")
        else:
            print(f"  FAIL  input: {pulses} F4 pulses were sent")
            failed = True
        if key == "released":
            print("  ok    final state: F4 was not left held")
        else:
            print("  FAIL  release: F4 remained held")
            failed = True
        if overlay == "up":
            print("  ok    receipt: LF2 opened its post-match overlay and left it open")
        else:
            print("  FAIL  ownership: the post-match overlay was dismissed or never appeared")
            failed = True

    if run.returncode != 0:
        print(f"  FAIL  game exited with status {run.returncode}")
        failed = True

    print("leave match: FAILED" if failed else "leave match: ok")
    if failed:
        print(f"        full log: {ROOT / 'scratch/logs/leave_match.log'}")
    return 1 if failed else 0


def save_log(output: str) -> None:
    path = ROOT / "scratch" / "logs" / "leave_match.log"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
