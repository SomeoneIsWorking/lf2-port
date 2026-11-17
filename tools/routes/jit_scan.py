#!/usr/bin/env python3
"""Run each reachable game state through the JIT and report what x86port refused there.

One hidden, unpaced run per state; the run-end `jit:` stats line and any JIT stop are the
report. A refusal ends the run (the direct engine has no fallback edge), so a state shows its
first refusal; the next one appears once x86port covers it.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "scratch" / "jit_scan"

CHARSELECT = (
    "south@charselect+58", "south@charselect+118", "south@charselect+178",
    "south@charselect+238", "up@charselect+298", "up@charselect+358",
    "south@charselect+418", "south@charselect+618", "south@charselect+838",
    "up@overlay+99", "up@overlay+159", "south@overlay+219",
)
# D>A, D^A, DvA, D>J, D^J, DvJ and plain attacks, walks and jumps: each fighter's moves.
MOVES = (
    ("west", "right", "south"), ("west", "up", "south"), ("west", "down", "south"),
    ("west", "right", "east"), ("west", "up", "east"), ("west", "down", "east"),
    ("right", "south"), ("left", "south"), ("east", "south"), ("right", "right", "south"),
)


def fight(start: int, end: int) -> tuple[str, ...]:
    pad = []
    frame = start
    while frame < end:
        for move in MOVES:
            for button in move:
                pad.append(f"{button}@match+{frame}")
                frame += 10
            frame += 30
    return tuple(item for item in pad if int(item.rsplit("+", 1)[1]) < end)


# Battle's team-setting screen and the championship bracket have no screen anchor of their
# own, so they are keyed off the screen before them.
BATTLE_TEAMS = ("south@charselect+960", "south@charselect+1060")
BRACKET = tuple(f"south@modemenu+{frame}" for frame in range(60, 1400, 60))


def mode_route(mode: str, setup: tuple[str, ...]) -> dict[str, str]:
    return {
        "LF2_MODE": mode,
        "LF2_VIRTUAL_PAD": ",".join(setup + fight(100, 3000)),
    }


STATES: dict[str, dict[str, str]] = {
    "boot": {},
    "vs": mode_route("vs", ("south@modemenu+60",) + CHARSELECT),
    "stage": mode_route("stage", ("south@modemenu+60",) + CHARSELECT),
    "championship1": mode_route("championship1", BRACKET),
    "championship2": mode_route("championship2", BRACKET),
    "battle": mode_route("battle", ("south@modemenu+60",) + CHARSELECT[:9] + BATTLE_TEAMS + CHARSELECT[9:]),
    "demo": {"LF2_MODE": "demo", "LF2_VIRTUAL_PAD": "south@modemenu+60"},
}

STATS = re.compile(r"blocks entered=\d+ translated=\d+; instructions translated=\d+; "
                   r"translation refusals=\d+; cache flushes=\d+; code bytes=\d+")
STOP = re.compile(r"(JIT [^\n]*at guest [0-9a-f]{8}: [^\n]*|unowned interception[^\n]*)")
SCREENS = re.compile(r"scripted input: screen (\w+) first up at frame (\d+)")
FIRED = re.compile(r"LF2_VIRTUAL_PAD: \d+ of \d+ items fired")


def run_state(executable: Path, game: Path, name: str, frames: int) -> str:
    env = {key: value for key, value in os.environ.items() if not key.startswith("LF2_")}
    env.update(SDL_AUDIODRIVER="dummy", LF2_WINDOW="hidden", LF2_UNPACED="1",
               LF2_QUIT_AFTER=str(frames), LF2_CONFIG="")
    env.update(STATES[name])
    log = OUTPUT / f"{name}.log"
    with log.open("w") as output:
        result = subprocess.run([str(executable), "lf2.exe"], cwd=game, env=env, stdout=output,
                                stderr=subprocess.STDOUT, timeout=900, check=False)
    text = payload_text(log.read_text(errors="replace"))
    stats = STATS.search(text)
    stop = STOP.search(text)
    screens = " ".join(f"{screen}@{frame}" for screen, frame in SCREENS.findall(text))
    fired = FIRED.search(text)
    return (f"{name}: exit={result.returncode}; {stats.group(0) if stats else 'no jit stats'}; "
            f"{stop.group(0) if stop else 'no stop'}; "
            f"screens {screens or 'none'}; {fired.group(0) if fired else 'no pad'}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "clang")
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--frames", type=int, default=3600)
    parser.add_argument("states", nargs="*", default=list(STATES))
    args = parser.parse_args()
    executable = args.build_dir.resolve() / "lf2"
    if not executable.is_file():
        raise SystemExit(f"{executable} is not built")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in args.states:
        if name not in STATES:
            raise SystemExit(f"unknown state {name}; known: {' '.join(STATES)}")
        print(run_state(executable, args.game.resolve(), name, args.frames), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
