#!/usr/bin/env python3
"""Offline positive and other-answer checks for tools/routes/parallax_jitter.py."""

from __future__ import annotations

from pathlib import Path
import os
import re
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "routes"))

import parallax_jitter  # noqa: E402
from parallax_jitter import Cadence, accept, mountain_roi  # noqa: E402
from parallax_jitter_test import (  # noqa: E402
    FRAME_SPECS,
    PAD_ACTIONS,
    authenticate_log,
    authentication_errors,
    camera_sequence_error,
    case_environment,
    create_run_directory,
)
from ppm import read_ppm  # noqa: E402


def main() -> int:
    assert parallax_jitter.read_ppm is read_ppm
    smooth = Cadence((120, 90, 110, 100))
    integer = Cadence((0, 0, 900, 0))
    assert accept(smooth, integer)[0]
    assert not accept(Cadence((0, 90, 110)), integer)[0]
    assert not accept(smooth, Cadence((90, 100, 110)))[0]
    assert not accept(smooth, Cadence((0, 0, 400, 0)))[0]
    assert not accept(Cadence((0, 0, 0)), Cadence((0, 0, 900)))[0]

    width, height = 3440, 1440
    pixels = bytes((index * 37) & 0xFF for index in range(width * height * 3))
    roi = mountain_roi(width, height, pixels)
    scale = height / 550.0
    rows = round(191.0 * scale) - round(147.0 * scale)
    columns = width - 2 * round(76.0 * scale)
    assert len(roi) == rows * columns * 3

    assert len(FRAME_SPECS) == 21
    assert len(PAD_ACTIONS) == 32
    env = case_environment(ROOT / "scratch" / "synthetic", False)
    assert env["LF2_ENGINE"] == "1"
    assert env["SDL_VIDEODRIVER"] == "offscreen"
    assert env["LF2_WINDOW_SIZE"] == "3440x1440"
    assert "LF2_BG_INTEGER_RASTER" not in env
    negative_env = case_environment(ROOT / "scratch" / "synthetic", True)
    assert negative_env["LF2_BG_INTEGER_RASTER"] == "1"

    allocation_root = ROOT / "scratch" / f"parallax_jitter_allocator_{os.getpid()}_{time.time_ns()}"
    first_run = create_run_directory(allocation_root, "collision_probe")
    assert first_run.is_dir()
    try:
        create_run_directory(allocation_root, "collision_probe")
    except FileExistsError:
        pass
    else:
        raise AssertionError("a second route run reused an existing evidence directory")
    first_run.rmdir()
    allocation_root.rmdir()

    camera = "\n".join(
        f"backdrop camera frame {frame} guest={frame - 100} draw={frame - 101} view=1314 stage=3200"
        for frame in range(1000, 1000 + len(FRAME_SPECS))
    )
    authenticated = "\n".join(
        (
            "LF2_VIRTUAL_PAD: 32 of 32 items fired",
            "stage preview: Lion_Forest is registry background 1",
            "stage preview: selection survived match initialization; background 1 owns gameplay and rendering",
            "engine: render targets are 3440x1440 output pixels",
            "widescreen: window 3440x1440 -> composition 1314x550 at scale 2.618, fills the window",
            camera,
        )
    )
    assert not authentication_errors(authenticated)
    accepted_auth = authenticate_log(authenticated)
    assert not accepted_auth[0]
    assert camera_sequence_error(accepted_auth[1], accepted_auth[1]) is None
    mismatched = authenticated.replace("guest=909 draw=908", "guest=999 draw=998")
    mismatched_auth = authenticate_log(mismatched)
    assert not mismatched_auth[0]
    assert camera_sequence_error(accepted_auth[1], mismatched_auth[1]) is not None
    assert "did not move the camera" in " ".join(
        authentication_errors(re.sub(r"guest=\d+ draw=\d+", "guest=4 draw=3", authenticated))
    )
    assert authentication_errors(authenticated.replace("Lion_Forest", "Brokeback_Clif"))
    print("parallax jitter analyser and route authentication: 23 checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
