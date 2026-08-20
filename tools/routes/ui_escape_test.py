#!/usr/bin/env python3
"""Prove real SDL Escape opens and closes the global RmlUi shell."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "ui_escape_test"
RUN_LOG = OUTPUT / "run.log"
TOOL_LOG = OUTPUT / "xdotool.log"


def wait_for_text(path: Path, text: str, seconds: float) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if path.exists() and text in path.read_text(errors="replace"):
            return True
        time.sleep(0.05)
    return False


def xdotool(*args: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        ["xdotool", *args], capture_output=True, text=True, check=False
    )
    with TOOL_LOG.open("a") as output:
        output.write(result.stderr)
    return result


def run_inside_xvfb(build: Path, game: Path) -> int:
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="x11",
        SDL_AUDIODRIVER="dummy",
        LF2_RMLUI_DEBUG="1",
        LF2_QUIT_AFTER="240",
    )
    with RUN_LOG.open("w") as output:
        process = subprocess.Popen(
            [str(build / "lf2"), "lf2.exe"],
            cwd=game,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
        try:
            wait_for_text(RUN_LOG, "startup: first menu frame presented", 10.0)
            search = xdotool("search", "--onlyvisible", "--name", "Little Fighter 2")
            window = search.stdout.splitlines()[0] if search.stdout.splitlines() else ""
            TOOL_LOG.write_text(f"window={window}\n" + TOOL_LOG.read_text(errors="replace"))
            if window:
                xdotool("windowfocus", "--sync", window)
                xdotool("key", "Escape")
                wait_for_text(
                    RUN_LOG, "rmlui menu command: escape=1 start=0 active=0", 5.0
                )
                xdotool("key", "Escape")
                wait_for_text(
                    RUN_LOG, "rmlui menu command: escape=1 start=0 active=1", 5.0
                )
                xdotool("keydown", "z")
                time.sleep(0.15)
                xdotool("keyup", "z")
            return process.wait(timeout=15.0)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            return 124


def check_results() -> int:
    log = RUN_LOG.read_text(errors="replace") if RUN_LOG.exists() else ""
    tool_log = TOOL_LOG.read_text(errors="replace") if TOOL_LOG.exists() else ""
    checks = (
        (
            tool_log.splitlines() and tool_log.splitlines()[0].removeprefix("window=").isdigit(),
            "the X11 game window received XTEST keyboard input",
            "xdotool could not find the visible game window",
        ),
        (
            log.count("rmlui physical key: vk=1b down\n") == 2,
            "SDL delivered two physical Escape down events",
            "SDL did not deliver both physical Escape presses",
        ),
        (
            "rmlui menu command: escape=1 start=0 active=0\n" in log
            and "rmlui menu command: escape=1 start=0 active=1\n" in log,
            "Escape opened the shell, then closed that same shell",
            "the physical Escape edge did not toggle both menu states",
        ),
        (
            "rmlui: 1 settings open(s), " in log and " rendered frame(s)" in log,
            "exactly one RmlUi document opened and rendered",
            "RmlUi did not report one rendered opening",
        ),
        (
            "scripted input: screen charselect first up at frame " in log,
            "a normal physical key still reached the game after RmlUi closed",
            "physical non-modal key forwarding did not reach character selection",
        ),
    )
    failed = False
    for passed, success, failure in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {success if passed else failure}")
        failed |= not passed
    print(f"physical Escape test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


def main(argv: list[str]) -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    TOOL_LOG.write_text("")

    if not (build / "lf2").is_file():
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77
    if shutil.which("xvfb-run") is None or shutil.which("xdotool") is None:
        print("SKIP: xvfb-run and xdotool are required")
        return 77

    if argv == ["--inside-xvfb"]:
        return run_inside_xvfb(build, game)

    print("global RmlUi: physical Escape opens and closes the shell...")
    process = subprocess.Popen(
        ["xvfb-run", "-a", sys.executable, str(Path(__file__).resolve()), "--inside-xvfb"],
        cwd=ROOT,
        env=os.environ.copy(),
        start_new_session=True,
    )
    try:
        process.wait(timeout=35.0)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    return check_results()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
