#!/usr/bin/env python3
"""Prove output scaling cannot feed back into the pre-fight panel's source mapping."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

from ppm import read_ppm

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "overlay_sampling_test"
GREEN = b"\x00\xff\x1e"
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
    )
)


def green_signature(width: int, height: int, pixels: bytes) -> tuple[int, int, int, int, int, int, int]:
    """Return exact-green count, longest runs, and inclusive bounds."""
    positions = {
        (offset // 3 % width, offset // 3 // width)
        for offset in range(0, len(pixels), 3)
        if pixels[offset : offset + 3] == GREEN
    }
    longest_row = longest_column = 0
    for x, y in positions:
        if (x - 1, y) not in positions:
            run = 1
            while (x + run, y) in positions:
                run += 1
            longest_row = max(longest_row, run)
        if (x, y - 1) not in positions:
            run = 1
            while (x, y + run) in positions:
                run += 1
            longest_column = max(longest_column, run)
    if not positions:
        return 0, 0, 0, -1, -1, -1, -1
    xs = [x for x, _y in positions]
    ys = [y for _x, y in positions]
    return len(positions), longest_row, longest_column, min(xs), min(ys), max(xs), max(ys)


def signature_selftest() -> None:
    pixels = bytearray(b"\0\0\0" * 20)
    for x, y in ((1, 1), (2, 1), (3, 1), (4, 1), (1, 2), (1, 3)):
        offset = (y * 5 + x) * 3
        pixels[offset : offset + 3] = GREEN
    if green_signature(5, 4, bytes(pixels)) != (6, 4, 3, 1, 1, 4, 3):
        raise RuntimeError("green-line detector cannot recognise a synthetic L")
    if green_signature(5, 4, b"\0\0\0" * 20) != (0, 0, 0, -1, -1, -1, -1):
        raise RuntimeError("green-line detector reports green in a black control")


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def run_case(binary: Path, game: Path, name: str, raster_destination: bool) -> tuple[int, str, Path] | None:
    case = OUTPUT / name
    case.mkdir(parents=True, exist_ok=True)
    for old in case.glob("frame_*.ppm"):
        old.unlink()
    log = case / "run.log"
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_ENGINE="0",
        LF2_ENGINE_DEBUG="1",
        LF2_RENDER_DEBUG="1",
        LF2_WINDOW_SIZE="3840x1975",
        LF2_OVERLAY_FORCE="3",
        LF2_VIRTUAL_PAD=PAD,
        LF2_FRAME_DUMP="@overlay+80",
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1100",
    )
    env.pop("LF2_TEXRECT_RASTER_DEST", None)
    if raster_destination:
        env["LF2_TEXRECT_RASTER_DEST"] = "1"
    with log.open("w") as output:
        process = subprocess.Popen(
            [str(binary), "lf2.exe"],
            cwd=game,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
        print(f"  launched {name} game PID {process.pid}", flush=True)
        try:
            returncode = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print(f"  FAIL  {name} exceeded 180 seconds")
            return None
    frames = sorted(case.glob("frame_*.ppm"))
    if len(frames) != 1:
        print(f"  FAIL  {name} captured {len(frames)} frames, expected one")
        return None
    return returncode, log.read_text(errors="replace"), frames[0]


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

    signature_selftest()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("overlay sampling: logical DirectDraw mapping versus raster-feedback negative...", flush=True)
    fixed = run_case(binary, game, "logical", False)
    negative = run_case(binary, game, "raster_feedback", True)
    if fixed is None or negative is None:
        return 1

    failed = False
    signatures: list[tuple[int, int, int, int, int, int, int]] = []
    for name, run in (("logical", fixed), ("raster-feedback negative", negative)):
        returncode, log, frame = run
        log = payload_text(log)
        if returncode != 0:
            print(f"  FAIL  {name} exited with status {returncode}")
            failed = True
        if "LF2_VIRTUAL_PAD: 10 of 10 items fired" not in log:
            print(f"  FAIL  {name} did not fire all ten route actions")
            failed = True
        classic_report = re.search(
            r"^engine: not drawing \([^)]+\)\. 0 frame\(s\), 0 quad\(s\) in 0 batch\(es\)",
            log,
            re.MULTILINE,
        )
        if classic_report is None:
            print(f"  FAIL  {name} did not prove the classic renderer was selected")
            failed = True
        try:
            width, height, pixels = read_ppm(frame)
        except ValueError as error:
            print(f"  FAIL  {name}: {error}")
            return 1
        if (width, height) != (3840, 1975):
            print(f"  FAIL  {name} capture is {width}x{height}")
            failed = True
        signature = green_signature(width, height, pixels)
        signatures.append(signature)
        bounds = "none" if signature[0] == 0 else f"x={signature[3]}..{signature[5]}, y={signature[4]}..{signature[6]}"
        print(
            f"  {name}: {signature[0]} green pixels, row run {signature[1]}, "
            f"column run {signature[2]}, bounds {bounds}"
        )

    if signatures[0] != (0, 0, 0, -1, -1, -1, -1):
        print("  FAIL  logical source mapping still exposes the green separator")
        failed = True
    if signatures[1] != (1080, 1002, 79, 548, 312, 1549, 390):
        print("  FAIL  raster-feedback negative did not reproduce the reported L-shaped separator")
        failed = True
    print(f"overlay sampling test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
