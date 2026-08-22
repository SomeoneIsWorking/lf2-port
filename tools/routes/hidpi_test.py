#!/usr/bin/env python3
"""Verify game and RmlUi drawable metrics on a simulated scaled 4K display."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "hidpi_test"
RUN_LOG = OUTPUT / "run.log"


def inside_compositor(build: Path, game: Path) -> int:
    time.sleep(3)
    scale = subprocess.run(
        ["kscreen-doctor", "output.1.scale.2"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if scale.returncode != 0:
        print("hidpi: kscreen-doctor could not set the scale", flush=True)
    time.sleep(2)

    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="wayland",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE="794x550",
        LF2_VIRTUAL_PAD="start@modemenu+20",
        LF2_RMLUI_DEBUG="1",
        LF2_RENDER_DEBUG="1",
        LF2_ENGINE_DEBUG="1",
        LF2_GLYPH_DEBUG="1",
        LF2_QUIT_AFTER="920",
    )
    return subprocess.run(
        [str(build / "lf2"), "lf2.exe"], cwd=game, env=env, check=False
    ).returncode


def run_nested(build: Path, game: Path) -> None:
    socket = f"wayland-lf2-hidpi-{os.getpid()}"
    command = [
        "kwin_wayland",
        "--virtual",
        "--width",
        "3840",
        "--height",
        "2160",
        "-s",
        socket,
        "--",
        sys.executable,
        str(Path(__file__).resolve()),
    ]
    nested_env = os.environ.copy()
    # KWin accepts the helper command but does not preserve a trailing mode argument. Use an
    # inherited diagnostic flag so the helper cannot recursively launch another compositor.
    nested_env["LF2_HIDPI_INSIDE"] = "1"
    with RUN_LOG.open("w") as output:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            env=nested_env,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            process.wait(timeout=200)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()


def check_results() -> int:
    text = RUN_LOG.read_text(errors="replace") if RUN_LOG.exists() else ""
    window = re.search(r"^window: .* points -> .* pixels.*$", text, re.MULTILINE)
    if window is None:
        print("  FAIL  the port never reported window geometry inside the nested session")
        return 1
    if "unscaled, so this run says nothing about HiDPI" in window.group(0):
        print(f"  FAIL  the nested output was not scaled: {window.group(0)}")
        return 1

    widescreen = re.search(r"^widescreen: window .*?$", text, re.MULTILINE)
    metrics = re.search(r"^rmlui metrics: .*?$", text, re.MULTILINE)
    glyphs = re.search(r"^glyph scale: .*?$", text, re.MULTILINE)
    engine_target = re.search(r"^engine: render targets are .*?$", text, re.MULTILINE)
    checks = (
        (
            "794x550 points -> 1588x1100 pixels" in window.group(0),
            f"scaled drawable: {window.group(0)}",
            f"expected 794x550 points -> 1588x1100 pixels: {window.group(0)}",
        ),
        (
            widescreen is not None
            and "window 1588x1100" in widescreen.group(0)
            and "composition 794x550" in widescreen.group(0)
            and "at scale 2.000" in widescreen.group(0)
            and "drawn into 1588x1100" in widescreen.group(0),
            f"native-resolution game composition: {widescreen.group(0) if widescreen else ''}",
            "the game composition did not retain its field of view at 2x drawable scale",
        ),
        (
            metrics is not None
            and "794x550 window points -> 1588x1100 drawable pixels" in metrics.group(0)
            and "content scale 2.00" in metrics.group(0)
            and "body font 32.0px" in metrics.group(0),
            f"RmlUi layout and FreeType raster size: {metrics.group(0) if metrics else ''}",
            "RmlUi did not use a 32px outline font in the 2x drawable",
        ),
        (
            "rmlui: 1 settings open(s), " in text,
            "the scaled-display run opened and rendered the real RmlUi document",
            "the scaled-display run never rendered an RmlUi opening",
        ),
        (
            glyphs is not None and "rasterised at 2.00x" in glyphs.group(0),
            f"game glyph raster size: {glyphs.group(0) if glyphs else ''}",
            "the default renderer did not rasterise game glyphs at the drawable scale",
        ),
        (
            "engine: DRAWING (ready)." in text,
            "the scaled-display run exercised the shipping GPU renderer",
            "the scaled-display run did not exercise the shipping GPU renderer",
        ),
        (
            engine_target is not None and "1588x1100 output pixels" in engine_target.group(0),
            f"native engine drawable target: {engine_target.group(0) if engine_target else ''}",
            "the native engine did not report a 1588x1100 target; requested glyph scale alone cannot prove full-resolution rasterization",
        ),
    )
    failed = False
    for passed, success, failure in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {success if passed else failure}")
        failed |= not passed
    print(f"hidpi test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


def main(argv: list[str]) -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    if not (build / "lf2").is_file():
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77
    if os.environ.get("LF2_HIDPI_INSIDE") == "1":
        return inside_compositor(build, game)
    if shutil.which("kwin_wayland") is None or shutil.which("kscreen-doctor") is None:
        print("SKIP: kwin_wayland and kscreen-doctor are required")
        return 77

    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("hidpi: game and RmlUi on a simulated 4K display at 200%...")
    run_nested(build, game)
    return check_results()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
