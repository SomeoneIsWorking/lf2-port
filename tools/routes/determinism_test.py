#!/usr/bin/env python3
"""Run the smoke route's inputs twice and require identical per-frame state digests.

This is the determinism a rollback session rests on: the same image and the same inputs give
the same guest state on every frame. The first differing frame names where to start tracing.
The two runs start together from one install, as two players on one machine would.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime_log import payload_line
from smoke_test import KEY_SCRIPT


# Logic frames; each digests every guest span.
FRAMES = 300
STATE = re.compile(r"^frame (\d+) logic=([0-9a-f]{16}) media=([0-9a-f]{16}) presented=(\d+)$")


def start(build: Path, game: Path, log_path: Path, extra: dict[str, str] | None = None) -> subprocess.Popen:
    env = os.environ.copy()
    env.update(
        SDL_AUDIODRIVER="dummy",
        LF2_WINDOW="hidden",
        LF2_UNPACED="1",
        LF2_STATE_HASH="1",
        LF2_WALL_EPOCH="1000000000",
        LF2_KEY_SCRIPT=KEY_SCRIPT,
        LF2_QUIT_AFTER=str(FRAMES),
    )
    env.update(extra or {})
    command = ["timeout", "-k", "5", "900", str(build / "lf2"), "lf2.exe"]
    with log_path.open("w") as output:
        return subprocess.Popen(command, cwd=game, env=env, stdout=output, stderr=subprocess.STDOUT)


def frame_states(log_path: Path) -> list[tuple[int, str, str, str]]:
    states = []
    for line in log_path.read_text(errors="replace").splitlines():
        match = STATE.match(payload_line(line))
        if match:
            states.append((int(match.group(1)), match.group(2), match.group(3), match.group(4)))
    return states


def run_once(build: Path, game: Path, log_path: Path,
             extra: dict[str, str] | None = None) -> tuple[int, list[tuple[int, str, str, str]]]:
    process = start(build, game, log_path, extra)
    return process.wait(), frame_states(log_path)


def first_difference(first: list, second: list, column: int) -> int | None:
    for a, b in zip(first, second):
        if a[column] != b[column]:
            return a[0]
    return None


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("BUILD") or root / "build" / "clang").resolve()
    game = Path(os.environ.get("GAME") or root / "game").resolve()
    out = root / "scratch" / "determinism"
    out.mkdir(parents=True, exist_ok=True)

    print(f"two runs of {FRAMES} frames at once with the smoke route's inputs...")
    runs = [start(build, game, out / f"run{n}.log") for n in (1, 2)]
    first_exit, second_exit = (run.wait() for run in runs)
    first, second = frame_states(out / "run1.log"), frame_states(out / "run2.log")
    if first_exit or second_exit:
        print(f"  FAIL  a run did not quit at frame {FRAMES} (exit {first_exit}, {second_exit}); see {out}")
        return 1
    if not first or not second:
        print(f"  FAIL  no frame-state lines (run1={len(first)}, run2={len(second)}); see {out}")
        return 1

    frames = min(len(first), len(second))
    logic = first_difference(first, second, 1)
    media = first_difference(first, second, 2)
    presented = first_difference(first, second, 3)
    print(f"  frames compared: {frames} (run1={len(first)}, run2={len(second)})")
    print(f"  logic: {'identical' if logic is None else f'first differs at frame {logic}'}")
    print(f"  media: {'identical' if media is None else f'first differs at frame {media}'}")
    print(f"  presents: {'identical' if presented is None else f'first differ at frame {presented}'}")
    return 0 if logic is None and presented is None and len(first) == len(second) else 1


if __name__ == "__main__":
    sys.exit(main())
