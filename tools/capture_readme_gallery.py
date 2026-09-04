#!/usr/bin/env python3
"""Capture the five README gallery candidates from the current LF2 build.

The game launches are deliberately serial. Raw presented-target PPMs, run logs, and converted
PNG candidates stay under ``scratch/readme_gallery``; this tool never writes tracked screenshots.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from typing import Mapping, Sequence

from build.scratch_clean import ScratchCleanError, empty_scratch_child
from routes.ppm import read_ppm


ROOT = Path(__file__).resolve().parent.parent
SCRATCH = ROOT / "scratch"


class GalleryError(RuntimeError):
    """A capture precondition, run, or output invariant failed."""


@dataclass(frozen=True)
class Candidate:
    filename: str
    dump_spec: str
    width: int
    height: int
    page_marker: str | None = None


@dataclass(frozen=True)
class CaptureRun:
    name: str
    window_size: str
    pad_actions: tuple[str, ...]
    candidates: tuple[Candidate, ...]
    quit_after: int
    timeout_seconds: int
    mode: str | None = None
    rmlui_debug: bool = False
    camera_debug: bool = False


def stage_pad_actions() -> tuple[str, ...]:
    """The deterministic Stage 1-1 PvE route used for the ultrawide showcase."""
    actions = [
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
    ]
    actions.extend(f"right@match+{frame}" for frame in range(60, 601, 30))
    actions.extend(
        f"south@match+{frame}"
        for frame in (180, 300, 420, 540, 660, 780, 900, 1020, 1140, 1260, 1380, 1500, 1620)
    )
    actions.extend(f"east@match+{frame}" for frame in (240, 720, 1200))
    return tuple(actions)


RUNS = (
    CaptureRun(
        name="demo",
        window_size="1920x1080",
        mode="demo",
        pad_actions=("south@modemenu+60",),
        candidates=(Candidate("demo-match-widescreen.png", "@match+120", 1920, 1080),),
        quit_after=400,
        timeout_seconds=300,
    ),
    CaptureRun(
        name="graphics",
        window_size="1920x1080",
        pad_actions=(
            "start@modemenu+60",
            "down@modemenu+140",
            "down@modemenu+180",
            "down@modemenu+220",
            "south@modemenu+260",
        ),
        candidates=(
            # The document opens on GAME before the first scripted Down. Opening emits the
            # metrics line; explicit tab changes emit ``rmlui page: ...`` below.
            Candidate("port-menu-overview.png", "@modemenu+96", 1920, 1080, "rmlui metrics:"),
            Candidate("port-menu-graphics.png", "@modemenu+316", 1920, 1080, "rmlui page: graphics"),
        ),
        quit_after=420,
        timeout_seconds=300,
        rmlui_debug=True,
    ),
    CaptureRun(
        name="controls",
        window_size="1920x1080",
        pad_actions=(
            "start@modemenu+60",
            "down@modemenu+140",
            "down@modemenu+180",
            "down@modemenu+220",
            "down@modemenu+260",
            "south@modemenu+300",
        ),
        candidates=(
            Candidate("port-menu-controls.png", "@modemenu+356", 1920, 1080, "rmlui page: controls"),
        ),
        quit_after=440,
        timeout_seconds=300,
        rmlui_debug=True,
    ),
    CaptureRun(
        name="stage",
        window_size="3440x1440",
        mode="stage",
        pad_actions=stage_pad_actions(),
        candidates=(Candidate("stage-mode-pve-ultrawide.png", "@match+720", 3440, 1440),),
        quit_after=2600,
        timeout_seconds=400,
        camera_debug=True,
    ),
)


def configured_path(value: str | Path) -> Path:
    path = Path(value)
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def validate_output_path(path: Path) -> Path:
    """Return a resolved scratch child, refusing tracked or broad cleanup targets."""
    resolved = path.resolve()
    scratch = SCRATCH.resolve()
    try:
        resolved.relative_to(scratch)
    except ValueError as error:
        raise GalleryError(f"output must be inside {scratch}, got {resolved}") from error
    if resolved == scratch:
        raise GalleryError("output must be a child of scratch, not the whole scratch tree")
    return resolved


def validate_recipes(runs: Sequence[CaptureRun] = RUNS) -> None:
    candidates = [candidate for run in runs for candidate in run.candidates]
    if len(candidates) != 5:
        raise GalleryError(f"gallery recipe must produce five candidates, found {len(candidates)}")
    filenames = [candidate.filename for candidate in candidates]
    if len(set(filenames)) != len(filenames):
        raise GalleryError("gallery recipe contains duplicate candidate filenames")
    for run in runs:
        if not run.pad_actions:
            raise GalleryError(f"{run.name}: capture route has no deterministic input")
        dump_specs = [candidate.dump_spec for candidate in run.candidates]
        if dump_specs != sorted(dump_specs, key=lambda spec: int(spec.rsplit("+", 1)[1])):
            raise GalleryError(f"{run.name}: candidates are not in chronological dump order")


def capture_environment(parent: Mapping[str, str], run: CaptureRun, dump_dir: Path) -> dict[str, str]:
    """Build a deterministic environment without inheriting unrelated LF2 diagnostics."""
    env = {key: value for key, value in parent.items() if not key.startswith("LF2_")}
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_CONFIG="",
        LF2_ENGINE="1",
        LF2_HD2D="on",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE=run.window_size,
        LF2_VIRTUAL_PAD=",".join(run.pad_actions),
        LF2_FRAME_DUMP=",".join(candidate.dump_spec for candidate in run.candidates),
        LF2_DUMP_DIR=str(dump_dir),
        LF2_QUIT_AFTER=str(run.quit_after),
    )
    if run.mode is not None:
        env["LF2_MODE"] = run.mode
    if run.rmlui_debug:
        env["LF2_RMLUI_DEBUG"] = "1"
    if run.camera_debug:
        env["LF2_CAMERA"] = "1"
    return env


def game_command(binary: Path) -> list[str]:
    return [str(binary), "lf2.exe"]


def png_command(magick: str, source: Path, destination: Path) -> list[str]:
    """Convert encoding only: no resize, crop, extent, or other geometry operation."""
    return [
        magick,
        str(source),
        "-strip",
        "-define",
        "png:compression-level=9",
        str(destination),
    ]


def read_png_size(path: Path) -> tuple[int, int]:
    try:
        with path.open("rb") as stream:
            header = stream.read(24)
    except OSError as error:
        raise GalleryError(f"cannot read converted PNG {path}: {error}") from error
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise GalleryError(f"{path} is not a PNG with an IHDR header")
    return struct.unpack(">II", header[16:24])


def require_inputs(build: Path, game: Path) -> tuple[Path, str]:
    binary = build / "lf2"
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise GalleryError(
            f"current build is missing or not executable: {binary}; run python3 tools/build/build.py"
        )
    executable = game / "lf2.exe"
    if not executable.is_file():
        raise GalleryError(
            f"game tree is missing {executable}; extract your LF2 v2.0a installer as documented"
        )
    magick = shutil.which("magick")
    if magick is None:
        raise GalleryError(
            "ImageMagick 'magick' is required for lossless PPM-to-PNG conversion; "
            "on Fedora run: sudo dnf install ImageMagick"
        )
    return binary, magick


def clean_output(output: Path) -> None:
    relative = output.relative_to(ROOT)
    try:
        _target, removed = empty_scratch_child(ROOT, relative)
    except ScratchCleanError as error:
        raise GalleryError(f"scoped scratch cleanup failed: {error}") from error
    print(f"gallery capture: cleared {relative} ({removed} entries removed)")


def terminate_exact_process(process: subprocess.Popen[bytes]) -> None:
    """Stop only the exact child launched by this tool, never a shared binary name."""
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def run_game(binary: Path, game: Path, run: CaptureRun, run_dir: Path) -> tuple[list[Path], str]:
    run_dir.mkdir(parents=True, exist_ok=True)
    log_path = run_dir / "run.log"
    command = game_command(binary)
    env = capture_environment(os.environ, run, run_dir)
    with log_path.open("wb") as log:
        try:
            process = subprocess.Popen(
                command,
                cwd=game,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
        except OSError as error:
            raise GalleryError(f"{run.name}: could not launch {binary}: {error}") from error
        try:
            returncode = process.wait(timeout=run.timeout_seconds)
        except subprocess.TimeoutExpired as error:
            terminate_exact_process(process)
            raise GalleryError(
                f"{run.name}: game process {process.pid} exceeded {run.timeout_seconds}s; see {log_path}"
            ) from error
        except BaseException:
            terminate_exact_process(process)
            raise
    if returncode != 0:
        raise GalleryError(f"{run.name}: game exited with status {returncode}; see {log_path}")

    text = log_path.read_text(errors="replace")
    action_count = len(run.pad_actions)
    fired = f"LF2_VIRTUAL_PAD: {action_count} of {action_count} items fired"
    if fired not in text:
        raise GalleryError(f"{run.name}: not every scripted action fired; see {log_path}")
    for candidate in run.candidates:
        if candidate.page_marker is not None and candidate.page_marker not in text:
            raise GalleryError(
                f"{run.name}: did not reach {candidate.page_marker!r}; see {log_path}"
            )
    frames = sorted(run_dir.glob("frame_*.ppm"))
    if len(frames) != len(run.candidates):
        raise GalleryError(
            f"{run.name}: expected {len(run.candidates)} frame dump(s), found {len(frames)}; see {log_path}"
        )
    return frames, text


def convert_candidate(magick: str, source: Path, destination: Path, candidate: Candidate) -> None:
    try:
        width, height, _pixels = read_ppm(source)
    except ValueError as error:
        raise GalleryError(str(error)) from error
    expected = (candidate.width, candidate.height)
    if (width, height) != expected:
        raise GalleryError(f"{source}: capture is {width}x{height}, expected {candidate.width}x{candidate.height}")
    try:
        result = subprocess.run(
            png_command(magick, source, destination),
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=120,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise GalleryError(f"PNG conversion did not complete for {source}: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise GalleryError(f"PNG conversion failed for {source}: {detail}")
    if read_png_size(destination) != expected:
        raise GalleryError(f"{destination}: PNG dimensions changed during conversion")


def print_plan(output: Path, build: Path, game: Path) -> None:
    print(f"binary:     {build / 'lf2'}")
    print(f"game tree:  {game}")
    print(f"raw/logs:   {output}")
    print(f"candidates: {output / 'candidates'}")
    for run in RUNS:
        print(f"\n{run.name}: {run.window_size}, {len(run.pad_actions)} input(s)")
        print(f"  LF2_FRAME_DUMP={','.join(candidate.dump_spec for candidate in run.candidates)}")
        for candidate in run.candidates:
            print(f"  -> {candidate.filename} ({candidate.width}x{candidate.height})")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture five current-build README screenshots into a scratch candidate directory."
    )
    parser.add_argument(
        "--output",
        default=os.environ.get("LF2_GALLERY_OUTPUT", "scratch/readme_gallery"),
        help="scratch child for raw dumps, logs, and candidates (default: scratch/readme_gallery)",
    )
    parser.add_argument(
        "--build-dir",
        default=os.environ.get("BUILD", "build/clang"),
        help="directory containing the current lf2 binary",
    )
    parser.add_argument(
        "--game-dir",
        default=os.environ.get("GAME", "game"),
        help="extracted game tree containing lf2.exe",
    )
    parser.add_argument("--plan", action="store_true", help="print the serial recipe without cleaning or launching")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        validate_recipes()
        output = validate_output_path(configured_path(args.output))
        build = configured_path(args.build_dir)
        game = configured_path(args.game_dir)
        if args.plan:
            print_plan(output, build, game)
            return 0

        binary, magick = require_inputs(build, game)
        clean_output(output)
        candidate_dir = output / "candidates"
        candidate_dir.mkdir(parents=True, exist_ok=True)
        for index, run in enumerate(RUNS, start=1):
            print(f"[{index}/{len(RUNS)}] capturing {run.name} at {run.window_size}...", flush=True)
            frames, _text = run_game(binary, game, run, output / run.name)
            for frame, candidate in zip(frames, run.candidates, strict=True):
                destination = candidate_dir / candidate.filename
                convert_candidate(magick, frame, destination, candidate)
                print(f"  {destination.name}: {candidate.width}x{candidate.height}")
        print(f"gallery candidates ready: {candidate_dir}")
        print("Tracked docs/screenshots files were not modified.")
        return 0
    except GalleryError as error:
        print(f"gallery capture: ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
