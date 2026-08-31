#!/usr/bin/env python3
"""Prove that LF2's shipping pointer route drives every pre-match menu."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "scratch" / "mouse-route"
DATA_BASE = 0x0044D000
DIFFICULTY = 0x00450C30


def scoped_clean(path: Path) -> None:
    resolved = path.resolve()
    scratch = (ROOT / "scratch").resolve()
    if resolved == scratch or scratch not in resolved.parents:
        raise RuntimeError(f"refusing to clean non-scoped output {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)


def require_input(path: Path, description: str, executable: bool = False) -> None:
    valid = path.is_file() and (not executable or os.access(path, os.X_OK))
    if not valid:
        print(f"SKIP: {description} is missing: {path}")
        raise SystemExit(77)


def read_difficulty(path: Path) -> int:
    data = path.read_bytes()
    offset = DIFFICULTY - DATA_BASE
    if len(data) < offset + 4:
        raise AssertionError(f"{path} is only {len(data)} bytes; difficulty state is not visible")
    return int.from_bytes(data[offset : offset + 4], "little", signed=True)


def main() -> int:
    build = Path(os.environ.get("BUILD", ROOT / "scratch" / "build-clang")).resolve()
    game = Path(os.environ.get("GAME", ROOT / "game")).resolve()
    binary = build / "lf2"
    require_input(binary, "LF2 binary", executable=True)
    require_input(game / "lf2.exe", "game tree")

    scoped_clean(OUTPUT)
    OUTPUT.mkdir(parents=True)
    clicks = ";".join(
        (
            "400,241@modemenu+60",
            "200,150@charselect+98",
            "200,150@charselect+248",
            "200,150@charselect+398",
            "150,120@overlay+60",
            "150,120@overlay+160",
            "150,25@overlay+260",
            "150,25@overlay+320",
        )
    )
    environment = dict(os.environ)
    environment.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_RENDERER="soft",
        LF2_SCREEN_HASH="1",
        LF2_CK_DEBUG="1",
        LF2_CLICK_SCRIPT=clicks,
        LF2_MEM_DUMP="@overlay+50,@overlay+100,@overlay+200",
        LF2_DUMP_DIR=str(OUTPUT),
        LF2_QUIT_AFTER="1000",
    )
    print("driving every pre-match menu from the pointer alone...")
    result = subprocess.run(
        [str(binary), "lf2.exe"], cwd=game, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=220, check=False,
    )
    log = result.stdout
    (OUTPUT / "mouse.log").write_text(log, encoding="utf-8")

    failures: list[str] = []
    screens_line = next((line for line in log.splitlines() if "scripted input: screens reached --" in line), "")
    for screen in ("charselect", "overlay", "match"):
        if f"{screen}@" not in screens_line:
            failures.append(f"never reached {screen}: {screens_line or 'no screen summary'}")
    fired_line = next((line for line in log.splitlines() if "LF2_CLICK_SCRIPT:" in line), "")
    if "8 of 8 items fired" not in fired_line:
        failures.append(f"not every click fired: {fired_line or 'no click summary'}")

    dumps = sorted(OUTPUT.glob("data_*.bin"))
    if len(dumps) != 3:
        failures.append(f"expected 3 overlay state dumps, found {len(dumps)}")
    else:
        measured = [read_difficulty(path) for path in dumps]
        if measured != [0, 2, 1]:
            failures.append(f"difficulty clicks produced {measured}, expected [0, 2, 1]")

    colour_key = next((line for line in reversed(log.splitlines()) if "colour-key:" in line), "")
    keyed = re.search(r"keyed blits=(\d+)", colour_key)
    if not keyed or int(keyed.group(1)) < 1000:
        failures.append(f"sprite drawing was not observed: {colour_key or 'no colour-key summary'}")
    if result.returncode != 0:
        failures.append(f"game exited with status {result.returncode}")
    abort = re.search(r"unimplemented opcode|fell off the end|Aborted", log)
    if abort:
        failures.append(f"runtime abort was logged: {abort.group(0)}")

    if failures:
        print("mouse route FAILED")
        for failure in failures:
            print(f"  FAIL  {failure}")
        print(f"  log: {OUTPUT / 'mouse.log'}")
        return 1
    print("  reached charselect, overlay, and match")
    print("  difficulty state changed 0 -> 2 -> 1 from two pointer clicks")
    print("  all 8 clicks fired; clean shutdown; no abort")
    print("mouse route PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
