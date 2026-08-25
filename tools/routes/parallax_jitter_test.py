#!/usr/bin/env python3
"""Prove full-resolution Lion Forest parallax preserves its fractional raster phase."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_ROOT = ROOT / "scratch" / "parallax_jitter_test"
FRAME_SPECS = tuple(f"@match+{frame}" for frame in range(270, 291))
PAD_ACTIONS = (
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
) + tuple(f"right@match+{frame}" for frame in range(60, 601, 30))

_CAMERA_RE = re.compile(
    r"backdrop camera frame (\d+) guest=(-?\d+) draw=(-?\d+) view=(\d+) stage=(\d+)"
)
CameraSequence = tuple[tuple[int, int, int, int], ...]


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def create_run_directory(parent: Path = OUTPUT_ROOT, token: str | None = None) -> Path:
    """Allocate one retained evidence directory; an existing run is never reused."""
    parent.mkdir(parents=True, exist_ok=True)
    if token is None:
        return Path(tempfile.mkdtemp(prefix="run_", dir=parent))
    run = parent / f"run_{token}"
    run.mkdir()
    return run


def case_environment(case: Path, integer_negative: bool) -> dict[str, str]:
    """Return the one controlled recipe used by both renderer arms."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("LF2_")}
    env.update(
        SDL_AUDIODRIVER="dummy",
        SDL_VIDEODRIVER="offscreen",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_ENGINE="1",
        LF2_HD2D="off",
        LF2_ENGINE_DEBUG="1",
        LF2_RENDER_DEBUG="1",
        LF2_CAMERA="1",
        LF2_MODE="vs",
        LF2_STAGE_PREVIEW="Lion_Forest",
        LF2_WINDOW_SIZE="3440x1440",
        LF2_VIRTUAL_PAD=",".join(PAD_ACTIONS),
        LF2_FRAME_DUMP=",".join(FRAME_SPECS),
        LF2_BLT_FRAME=",".join(FRAME_SPECS),
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1800",
    )
    if integer_negative:
        env["LF2_BG_INTEGER_RASTER"] = "1"
    return env


def authenticate_log(log: str) -> tuple[list[str], CameraSequence]:
    """Authenticate the stage, renderer, route, target, and moving camera in one arm."""
    log = payload_text(log)
    errors: list[str] = []
    required = (
        ("LF2_VIRTUAL_PAD: 32 of 32 items fired", "all 32 route actions did not fire"),
        ("stage preview: Lion_Forest is registry background 1", "Lion Forest was not selected"),
        (
            "stage preview: selection survived match initialization",
            "Lion Forest did not survive match initialization",
        ),
        ("background 1 owns gameplay and rendering", "Lion Forest did not own match rendering"),
        ("engine: render targets are 3440x1440 output pixels", "the engine target was not 3440x1440"),
    )
    errors.extend(why for needle, why in required if needle not in log)

    geometry = re.search(r"^widescreen: window 3440x1440 .*?$", log, re.MULTILINE)
    if geometry is None or not all(
        value in geometry.group(0)
        for value in ("composition 1314x550", "at scale 2.618", "fills the window")
    ):
        errors.append("the 3440x1440 full-output composition was not authenticated")

    cameras: dict[int, tuple[int, int, int, int]] = {}
    for match in _CAMERA_RE.finditer(log):
        frame, guest, draw, view, stage = map(int, match.groups())
        value = (guest, draw, view, stage)
        if frame in cameras and cameras[frame] != value:
            errors.append(f"camera frame {frame} has conflicting trace records")
        cameras[frame] = value
    ordered_cameras = tuple(cameras[frame] for frame in sorted(cameras))
    if len(ordered_cameras) != len(FRAME_SPECS):
        errors.append(f"camera trace covered {len(cameras)} frames, expected {len(FRAME_SPECS)}")
    elif any(view != 1314 or stage != 3200 for _guest, _draw, view, stage in ordered_cameras):
        errors.append("camera trace did not use Lion Forest's 3200-wide stage and 1314-wide view")
    elif len({(guest, draw) for guest, draw, _view, _stage in ordered_cameras}) < 2:
        errors.append("the authenticated capture interval did not move the camera")
    return errors, ordered_cameras


def authentication_errors(log: str) -> list[str]:
    return authenticate_log(log)[0]


def camera_sequence_error(accepted: CameraSequence, negative: CameraSequence) -> str | None:
    if accepted == negative:
        return None
    return "accepted and integer-negative arms did not use the same ordered camera trajectory"


def run_case(
    binary: Path, game: Path, evidence_dir: Path, name: str, integer_negative: bool
) -> tuple[Path, CameraSequence] | None:
    case = evidence_dir / name
    case.mkdir(parents=True, exist_ok=True)
    log_path = case / "run.log"
    with log_path.open("w") as log_stream:
        process = subprocess.Popen(
            [str(binary), "lf2.exe"],
            cwd=game,
            env=case_environment(case, integer_negative),
            stdout=log_stream,
            stderr=subprocess.STDOUT,
        )
        print(f"  launched {name} game PID {process.pid}", flush=True)
        try:
            returncode = process.wait(timeout=240)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print(f"  FAIL  {name} PID {process.pid} exceeded 240 seconds")
            return None

    if returncode != 0:
        print(f"  FAIL  {name} exited with status {returncode}")
        return None
    log = log_path.read_text(errors="replace")
    errors, cameras = authenticate_log(log)
    frames = sorted(case.glob("frame_*.ppm"))
    if len(frames) != len(FRAME_SPECS):
        errors.append(f"captured {len(frames)} frames, expected {len(FRAME_SPECS)}")
    for error in errors:
        print(f"  FAIL  {name}: {error}")
    return None if errors else (case, cameras)


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

    try:
        output = create_run_directory()
    except OSError as error:
        print(f"FAIL: could not allocate retained parallax-jitter evidence: {error}")
        return 1
    print(f"parallax jitter: evidence retained at {output.relative_to(ROOT)}")

    print("parallax jitter: exact-phase renderer versus integer-raster negative...", flush=True)
    accepted = run_case(binary, game, output, "accepted", False)
    negative = run_case(binary, game, output, "integer_negative", True)
    if accepted is None or negative is None:
        return 1
    mismatch = camera_sequence_error(accepted[1], negative[1])
    if mismatch:
        print(f"FAIL: {mismatch}")
        return 1

    analyzer = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "routes" / "parallax_jitter.py"),
            str(accepted[0]),
            str(negative[0]),
        ],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    print(analyzer.stdout, end="")
    print(f"parallax-jitter route {'PASSED' if analyzer.returncode == 0 else 'FAILED'}")
    return analyzer.returncode


if __name__ == "__main__":
    raise SystemExit(main())
