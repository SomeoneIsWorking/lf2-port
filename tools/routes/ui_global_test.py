#!/usr/bin/env python3
"""Open RmlUi everywhere and prove that its match document freezes world simulation."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess

from ppm import read_ppm


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "ui_global_test"

KEY = "0x1B@modemenu+20,0x1B@modemenu+60"
PAD_ACTIONS = (
        "south@modemenu+100",
        "start@charselect+20",
        "east@charselect+60",
        "south@charselect+100",
        "south@charselect+160",
        "south@charselect+220",
        "south@charselect+280",
        "up@charselect+340",
        "up@charselect+400",
        "south@charselect+460",
        "south@charselect+660",
        "south@charselect+880",
        "start@overlay+20",
        "east@overlay+60",
        "up@overlay+100",
        "up@overlay+160",
        "south@overlay+220",
        "start@match+300",
        "east@match+360",
)
PAD = ",".join(PAD_ACTIONS)
CONTROL_PAD = ",".join(PAD_ACTIONS[:-2])

# Pre-open, two modal frames, and the first three frames after mapped Cancel is processed. The
# route's +360 action reaches RmlUi after the +360/+361 dumps; +362 is the first hidden frame.
# Starting there tests the transition itself rather than two still-correct modal frames.
DUMP_SPEC = "@match+299,@match+305,@match+359,@match+362,@match+363,@match+364"
CONTROL_DUMP_SPEC = "@match+305,@match+359,@match+362,@match+363,@match+364"


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def changed_fraction(left: bytes, right: bytes) -> float:
    if len(left) != len(right):
        return 1.0
    changed = sum(left[offset : offset + 3] != right[offset : offset + 3]
                  for offset in range(0, len(left), 3))
    return changed / (len(left) // 3)


def side_bands(pixels: bytes, width: int, height: int, band_width: int = 400) -> bytes:
    """Pixels outside RmlUi's centered 900px panel, where only the game may draw."""
    stride = width * 3
    side = band_width * 3
    return b"".join(
        pixels[y * stride : y * stride + side]
        + pixels[(y + 1) * stride - side : (y + 1) * stride]
        for y in range(height)
    )


def run_case(binary: Path, game: Path, name: str, pad: str, dump_spec: str) -> tuple[subprocess.CompletedProcess[bytes], str, list[Path]] | None:
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
        LF2_WINDOW_SIZE="1920x1080",
        LF2_KEY_SCRIPT=KEY,
        LF2_VIRTUAL_PAD=pad,
        LF2_RMLUI_DEBUG="1",
        LF2_FRAME_DUMP=dump_spec,
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="2450",
    )
    with log.open("w") as output:
        try:
            result = subprocess.run(
                [str(binary), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"  FAIL  the {name} route exceeded 300 seconds")
            return None
    return result, log.read_text(errors="replace"), sorted(case.glob("frame_*.ppm"))


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

    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("global RmlUi: checking modal transitions against a matching no-modal run...", flush=True)
    modal_run = run_case(binary, game, "modal", PAD, DUMP_SPEC)
    control_run = run_case(binary, game, "control", CONTROL_PAD, CONTROL_DUMP_SPEC)
    if modal_run is None or control_run is None:
        return 1
    result, text, frames = modal_run
    control_result, control_text, control_frames = control_run
    checks: list[tuple[bool, str]] = []
    report = re.search(r"rmlui: (\d+) settings open\(s\)", text)
    checks.append((report is not None and report.group(1) == "4",
                   "one direct shell opened on modemenu, charselect, overlay, and match"))
    for screen in ("modemenu", "charselect", "overlay", "match"):
        checks.append((re.search(rf"screens reached --.*{screen}@", text) is not None,
                       f"the route reached {screen}"))
    checks.append(("LF2_KEY_SCRIPT: 2 of 2 items fired" in text and
                   "LF2_VIRTUAL_PAD: 19 of 19 items fired" in text,
                   "every open, close, and route action fired"))
    checks.append((result.returncode == 0, f"the game exited cleanly ({result.returncode})"))
    checks.append(("LF2_KEY_SCRIPT: 2 of 2 items fired" in control_text and
                   "LF2_VIRTUAL_PAD: 17 of 17 items fired" in control_text,
                   "the no-match-modal control fired every matching route action"))
    checks.append((control_result.returncode == 0,
                   f"the no-match-modal control exited cleanly ({control_result.returncode})"))

    checks.append((len(frames) == 6, f"all six transition frames were captured ({len(frames)})"))
    checks.append((len(control_frames) == 5,
                   f"all five matching control frames were captured ({len(control_frames)})"))
    if len(frames) == 6 and len(control_frames) == 5:
        decoded = [read_ppm(frame) for frame in frames]
        control = [read_ppm(frame) for frame in control_frames]
        checks.append((all(width == 1920 and height == 1080 for width, height, _ in decoded + control),
                       "GPU readback retained the 1920x1080 output resolution"))
        modal = decoded[1][2]
        modal_later = decoded[2][2]
        closed_frames = [decoded[index][2] for index in range(3, 6)]
        control_modal_times = [control[0][2], control[1][2]]
        checks.append((changed_fraction(modal, control_modal_times[0]) >= 0.05,
                       "the captured modal visibly differs from the no-modal control"))
        modal_sides = side_bands(modal, 1920, 1080)
        modal_later_sides = side_bands(modal_later, 1920, 1080)
        control_sides = side_bands(control_modal_times[0], 1920, 1080)
        control_later_sides = side_bands(control_modal_times[1], 1920, 1080)
        checks.append((modal_sides == modal_later_sides,
                       "world pixels outside RmlUi stay byte-identical while the match is paused"))
        control_motion = changed_fraction(control_sides, control_later_sides)
        checks.append((control_motion >= 0.05,
                       f"the no-modal control changes at the same interval ({control_motion:.6f})"))
        checks.append((changed_fraction(modal_later, closed_frames[0]) >= 0.05,
                       "mapped Cancel removed the modal in the first hidden frame (+362)"))
        resumed_motion = [changed_fraction(closed_frames[i], closed_frames[i + 1])
                          for i in range(len(closed_frames) - 1)]
        checks.append((all(value >= 0.001 for value in resumed_motion),
                       "world pixels resume changing immediately after close "
                       f"({', '.join(f'{value:.6f}' for value in resumed_motion)})"))

    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    print(f"global RmlUi test {'FAILED' if failed else 'PASSED'}")
    if failed:
        print(f"        logs and transition frames: {OUTPUT}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
