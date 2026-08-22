#!/usr/bin/env python3
"""Prove the native engine rasterises at 3840x1975 and covers both output edges."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess

from ppm import read_ppm


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "fullres_test"
RUN_LOG = OUTPUT / "run.log"


def black_edge_pixels(width: int, height: int, pixels: bytes) -> tuple[int, int]:
    left = right = 0
    row_bytes = width * 3
    for y in range(height):
        row = y * row_bytes
        left += pixels[row : row + 3] == b"\0\0\0"
        edge = row + (width - 1) * 3
        right += pixels[edge : edge + 3] == b"\0\0\0"
    return left, right


def edge_reader_selftest() -> None:
    covered = bytes((1, 2, 3)) * 6
    if black_edge_pixels(3, 2, covered) != (0, 0):
        raise RuntimeError("edge checker rejects a covered synthetic frame")
    exposed = b"\0\0\0" + bytes((1, 2, 3)) * 2
    exposed += b"\0\0\0" + bytes((1, 2, 3)) * 2
    if black_edge_pixels(3, 2, exposed) != (2, 0):
        raise RuntimeError("edge checker cannot detect a synthetic black band")


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

    edge_reader_selftest()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for old in OUTPUT.glob("frame_*.ppm"):
        old.unlink()

    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE="3840x1975",
        LF2_FRAME_DUMP="@modemenu+60",
        LF2_DUMP_DIR=str(OUTPUT),
        LF2_RENDER_DEBUG="1",
        LF2_ENGINE_DEBUG="1",
        LF2_QUIT_AFTER="920",
    )

    print("full resolution: 1069.367 world pixels into a 3840x1975 drawable...", flush=True)
    with RUN_LOG.open("w") as output:
        try:
            result = subprocess.run(
                [str(binary), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=180,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print("  FAIL  the route exceeded 180 seconds")
            return 1

    failed = result.returncode != 0
    if failed:
        print(f"  FAIL  game exited with status {result.returncode}")

    text = RUN_LOG.read_text(errors="replace")
    geometry = re.search(r"^widescreen: window 3840x1975 .*?$", text, re.MULTILINE)
    geometry_ok = geometry is not None and all(
        part in geometry.group(0)
        for part in (
            "composition 1070x550",
            "at scale 3.591",
            "drawn into 3842x1975 at (-1,0)",
            "fills the window",
        )
    )
    if geometry_ok:
        print(f"  ok    integral world view covers the drawable: {geometry.group(0)}")
    else:
        print(f"  FAIL  composition did not cover 3840x1975: {geometry.group(0) if geometry else 'no report'}")
        failed = True

    target = re.search(r"^engine: render targets are (\d+)x(\d+) output pixels$", text, re.MULTILINE)
    if target is not None and target.groups() == ("3840", "1975"):
        print("  ok    the engine itself reports a 3840x1975 raster target")
    else:
        print(f"  FAIL  engine target is not 3840x1975: {target.group(0) if target else 'no target report'}")
        failed = True

    frames = sorted(OUTPUT.glob("frame_*.ppm"))
    if len(frames) != 1:
        print(f"  FAIL  expected one anchored frame, found {len(frames)}")
        return 1
    try:
        width, height, pixels = read_ppm(frames[0])
    except ValueError as error:
        print(f"  FAIL  {frames[0].name}: {error}")
        return 1
    if (width, height) != (3840, 1975):
        print(f"  FAIL  capture is {width}x{height}, expected 3840x1975")
        failed = True
    left, right = black_edge_pixels(width, height, pixels)
    if left == 0 and right == 0:
        print("  ok    all 1975 pixels in both outer columns are covered")
    else:
        print(f"  FAIL  exposed black band: {left} black left-edge pixels, {right} right-edge pixels")
        failed = True

    print(f"full-resolution test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
