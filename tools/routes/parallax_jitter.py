#!/usr/bin/env python3
"""Measure the Lion Forest mountain's scrolling cadence at full output resolution."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import sys

from ppm import read_ppm


@dataclass(frozen=True)
class Cadence:
    changes: tuple[int, ...]

    @property
    def stalls(self) -> int:
        return sum(value == 0 for value in self.changes)

    @property
    def peak(self) -> int:
        return max(self.changes, default=0)


def mountain_roi(width: int, height: int, pixels: bytes) -> bytes:
    """The keyed upper mountain strip, away from its clipped horizontal edges."""
    scale = height / 550.0
    top = round(147.0 * scale)
    bottom = round(191.0 * scale)
    margin = round(76.0 * scale)
    if bottom <= top or margin * 2 >= width:
        raise ValueError("the frame is too small for the Lion Forest mountain probe")
    return b"".join(
        pixels[(row * width + margin) * 3 : (row * width + width - margin) * 3]
        for row in range(top, bottom)
    )


def measure(paths: list[Path]) -> Cadence:
    if len(paths) < 3:
        raise ValueError("a scrolling cadence needs at least three frames")
    first_size: tuple[int, int] | None = None
    previous: bytes | None = None
    changes: list[int] = []
    for path in paths:
        width, height, pixels = read_ppm(path)
        if first_size is None:
            first_size = (width, height)
        elif first_size != (width, height):
            raise ValueError("the scrolling frames have different dimensions")
        roi = mountain_roi(width, height, pixels)
        if previous is not None:
            changes.append(sum(before != after for before, after in zip(previous, roi)))
        previous = roi
    return Cadence(tuple(changes))


def accept(smooth: Cadence, integer: Cadence) -> tuple[bool, str]:
    if smooth.stalls:
        return False, f"accepted renderer stalled on {smooth.stalls} transition(s)"
    if not integer.stalls:
        return False, "integer-only negative never stalled, so it did not expose the defect"
    if smooth.peak <= 0:
        return False, "accepted frames never changed, so they do not exercise scrolling"
    if integer.peak <= smooth.peak * 4:
        return False, "integer-only negative did not produce a magnified catch-up jump"
    return True, "ok"


def frames(directory: Path) -> list[Path]:
    return sorted(directory.glob("frame_*.ppm"))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: parallax_jitter.py ACCEPTED_DIR INTEGER_NEGATIVE_DIR", file=sys.stderr)
        return 2
    try:
        smooth = measure(frames(Path(argv[0])))
        integer = measure(frames(Path(argv[1])))
        ok, why = accept(smooth, integer)
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "parallax jitter: "
        f"accepted stalls={smooth.stalls}, range={min(smooth.changes)}..{smooth.peak}; "
        f"integer negative stalls={integer.stalls}, range={min(integer.changes)}..{integer.peak}"
    )
    if not ok:
        print(f"FAIL: {why}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
