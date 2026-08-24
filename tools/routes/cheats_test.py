#!/usr/bin/env python3
"""Activate F6 through the real Cheats pane and prove the guest handler consumed it."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess

from ui_global_test import PAD_ACTIONS


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "cheats_test"
DATA_BASE = 0x44D000


def word(data: bytes, address: int) -> int:
    offset = address - DATA_BASE
    return int.from_bytes(data[offset : offset + 4], "little")


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
    for old in OUTPUT.glob("data_*.bin"):
        old.unlink()
    pad = [
        *PAD_ACTIONS[:-2],
        "start@match+300",
        "down@match+340",
        "down@match+380",
        "down@match+420",
        "down@match+460",
        "down@match+500",
        "down@match+540",
        "south@match+580",
        "down@match+620",
        "down@match+660",
        "south@match+700",
    ]
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_VIRTUAL_PAD=",".join(pad),
        LF2_RMLUI_DEBUG="1",
        LF2_MEM_DUMP="@match+299,@match+760",
        LF2_DUMP_DIR=str(OUTPUT),
        LF2_QUIT_AFTER="2350",
    )
    print("cheats: activating Unlimited MP through the real RmlUi pane...", flush=True)
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
    except subprocess.TimeoutExpired:
        print("  FAIL  the route exceeded 300 seconds")
        return 1

    log = OUTPUT / "run.log"
    log.write_text(run.stdout, encoding="utf-8")
    dumps = sorted(OUTPUT.glob("data_*.bin"))
    checks: list[tuple[bool, str]] = [
        (run.returncode == 0, f"the game exited cleanly ({run.returncode})"),
        (f"LF2_VIRTUAL_PAD: {len(pad)} of {len(pad)} items fired" in run.stdout,
         "every route and RmlUi action fired"),
        ("rmlui page: cheats" in run.stdout, "controller navigation opened the Cheats pane"),
        ("cheat: F6 Unlimited MP, 1 activation(s), key released" in run.stdout,
         "the typed RmlUi command produced one released F6 pulse"),
        (len(dumps) == 2, f"both guest-state snapshots were written ({len(dumps)})"),
    ]
    if len(dumps) == 2:
        before, after = (path.read_bytes() for path in dumps)
        checks.extend(
            [
                (word(after, 0x450C18) == word(before, 0x450C18) + 1,
                 "LF2's own F6 usage counter incremented exactly once"),
                (word(after, 0x44D034) == 1 - word(before, 0x44D034),
                 "LF2's own unlimited-MP state toggled"),
            ]
        )

    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    if failed:
        print(f"cheats test FAILED; log and dumps: {OUTPUT}")
    else:
        print("cheats test PASSED")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
