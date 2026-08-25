#!/usr/bin/env python3
"""Measure Lion Forest's no-scale wide backdrop contract in two PPM captures."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys

from ppm import read_ppm

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text


FAR_SPAN = 800
BACKDROP_TOP = 128
FAR_BOTTOM = 198
MOUNTAIN_TOP = 147
KEY_COLOURS = {(0, 0, 0), (0, 255, 0)}


@dataclass(frozen=True)
class BackdropMetrics:
    key_holes: int
    far_join_excess: int
    mountain_1100_join_excess: int
    mountain_1400_join_excess: int
    camera_changed_bytes: int


@dataclass(frozen=True)
class ScrollMetrics:
    key_holes: int
    far_join_excess: int
    mountain_1100_join_excess: int
    mountain_1400_join_excess: int
    static_changed_bytes: int
    scrolling_changed_bytes: int


@dataclass(frozen=True)
class TraceMetrics:
    first_camera: int
    second_camera: int
    first_draw_camera: int
    second_draw_camera: int
    first_mountain_offset: int
    second_mountain_offset: int
    main_blits: int
    continuation_blits: int


def _pixel(pixels: bytes, width: int, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * width + x) * 3
    return tuple(pixels[offset : offset + 3])  # type: ignore[return-value]


def _join_excess(pixels: bytes, width: int, x: int, top: int, bottom: int) -> int:
    if x < 2 or x + 1 >= width:
        return 0

    def column_delta(right: int) -> int:
        return sum(
            sum(abs(a - b) for a, b in zip(_pixel(pixels, width, right - 1, y), _pixel(pixels, width, right, y)))
            for y in range(top, bottom)
        )

    join = column_delta(x)
    natural_neighbour = min(column_delta(x - 1), column_delta(x + 1))
    return max(0, join - natural_neighbour)


def measure(width: int, height: int, first: bytes, second: bytes) -> BackdropMetrics:
    expected = width * height * 3
    if width <= FAR_SPAN or height < FAR_BOTTOM or len(first) != expected or len(second) != expected:
        raise ValueError("captures do not contain a complete wide Lion Forest backdrop band")

    far_left = 0
    far_right = FAR_SPAN
    key_holes = sum(
        _pixel(first, width, x, y) in KEY_COLOURS
        for y in range(BACKDROP_TOP, FAR_BOTTOM)
        for x in range(width)
    )
    far_join = _join_excess(first, width, far_right, BACKDROP_TOP, MOUNTAIN_TOP)
    mountain_1100_join = _join_excess(first, width, 1100, 147, FAR_BOTTOM)
    mountain_1400_join = _join_excess(first, width, 1400, 155, FAR_BOTTOM)
    start = BACKDROP_TOP * width * 3
    end = FAR_BOTTOM * width * 3
    changed = sum(a != b for a, b in zip(first[start:end], second[start:end]))
    return BackdropMetrics(key_holes, far_join, mountain_1100_join, mountain_1400_join, changed)


def measure_scroll(width: int, height: int, first: bytes, second: bytes) -> ScrollMetrics:
    """Check two genuinely different camera frames without requiring moving pixels to agree."""
    expected = width * height * 3
    if width <= 1101 or height < FAR_BOTTOM or len(first) != expected or len(second) != expected:
        raise ValueError("captures do not contain a complete scrolling Lion Forest backdrop band")

    def holes(pixels: bytes) -> int:
        return sum(
            _pixel(pixels, width, x, y) in KEY_COLOURS
            for y in range(BACKDROP_TOP, FAR_BOTTOM)
            for x in range(width)
        )

    far_join = max(
        _join_excess(first, width, FAR_SPAN, BACKDROP_TOP, MOUNTAIN_TOP),
        _join_excess(second, width, FAR_SPAN, BACKDROP_TOP, MOUNTAIN_TOP),
    )
    mountain_1100_join = max(
        _join_excess(first, width, 1100, 147, FAR_BOTTOM),
        _join_excess(second, width, 1100, 147, FAR_BOTTOM),
    )
    # The 1400 join is outside any view narrow enough for that plane to scroll. Keep the
    # check live when it is visible, and let the trace prove its clipped native rectangles
    # when it is not.
    mountain_1400_join = max(
        _join_excess(first, width, 1400, 155, FAR_BOTTOM),
        _join_excess(second, width, 1400, 155, FAR_BOTTOM),
    )
    static_start = BACKDROP_TOP * width * 3
    static_end = MOUNTAIN_TOP * width * 3
    moving_start = 155 * width * 3
    moving_end = FAR_BOTTOM * width * 3
    static_changed = sum(a != b for a, b in zip(first[static_start:static_end], second[static_start:static_end]))
    scrolling_changed = sum(a != b for a, b in zip(first[moving_start:moving_end], second[moving_start:moving_end]))
    return ScrollMetrics(
        holes(first) + holes(second),
        far_join,
        mountain_1100_join,
        mountain_1400_join,
        static_changed,
        scrolling_changed,
    )


_CAMERA_RE = re.compile(
    r"backdrop camera frame (\d+) guest=(-?\d+) draw=(-?\d+) view=(\d+) stage=(\d+)"
)
_LAYER_RE = re.compile(
    r"backdrop layer frame (\d+) index=(\d+) span=(\d+) x=(-?\d+) off=(-?\d+) flags=(\d+)"
)
_BLIT_RE = re.compile(
    r"^blt \d+ .* rect=\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\) "
    r"src=.* srect=(?:NULL)?\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\)"
)
_NATIVE_RE = re.compile(
    r"backdrop native frame (\d+) mirror=[01] dst=\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\) "
    r"src=\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\)"
)


def measure_trace(log: str) -> TraceMetrics:
    """Prove the captures used distinct cameras and every backdrop rectangle stayed 1:1."""
    log = payload_text(log)
    cameras: dict[int, tuple[int, int]] = {}
    offsets: dict[int, list[int]] = {}
    pending: tuple[int, int, int, int] | None = None
    main_blits = 0
    continuation_blits = 0

    for line in log.splitlines():
        camera = _CAMERA_RE.search(line)
        if camera:
            frame, guest, draw = map(int, camera.group(1, 2, 3))
            value = (guest, draw)
            if frame in cameras and cameras[frame] != value:
                raise ValueError(f"frame {frame} has conflicting camera records")
            cameras[frame] = value
            continue

        layer = _LAYER_RE.search(line)
        if layer:
            frame, _index, span, _x, off, _flags = map(int, layer.groups())
            pending = (frame, span, off, int(_index))
            if span == 1400:
                offsets.setdefault(frame, []).append(off)
            continue

        native = _NATIVE_RE.search(line)
        if native:
            frame, dl, dt, dr, db, sl, st, sr, sb = map(int, native.groups())
            if abs(dr - dl) != abs(sr - sl) or abs(db - dt) != abs(sb - st):
                raise ValueError(f"frame {frame} contains a scaled native continuation")
            continuation_blits += 1
            continue

        blit = _BLIT_RE.search(line)
        if pending and blit:
            frame, _span, _off, _index = pending
            dl, dt, dr, db, sl, st, sr, sb = map(int, blit.groups())
            if dr != dl or db != dt:
                if abs(dr - dl) != abs(sr - sl) or abs(db - dt) != abs(sb - st):
                    raise ValueError(f"frame {frame} layer {_index} contains a scaled main blit")
                main_blits += 1
                pending = None

    if pending:
        raise ValueError("the final traced backdrop layer has no corresponding blit")
    frames = sorted(cameras)
    if len(frames) != 2:
        raise ValueError(f"expected two selected camera frames, found {len(frames)}")
    first, second = frames
    if cameras[first] == cameras[second]:
        raise ValueError("the selected frames used the same camera")
    if first not in offsets or second not in offsets:
        raise ValueError("the selected frames do not include the scrolling 1400 plane")
    first_offsets = set(offsets[first])
    second_offsets = set(offsets[second])
    if len(first_offsets) != 1 or len(second_offsets) != 1:
        raise ValueError("the 1400-plane pieces did not share one authored offset")
    first_offset = next(iter(first_offsets))
    second_offset = next(iter(second_offsets))
    if first_offset == second_offset:
        raise ValueError("the selected cameras did not move the 1400 plane")
    if main_blits < 10 or continuation_blits == 0:
        raise ValueError("the trace does not cover every backdrop layer and its continuations")
    return TraceMetrics(
        cameras[first][0], cameras[second][0], cameras[first][1], cameras[second][1],
        first_offset, second_offset, main_blits, continuation_blits,
    )


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print("usage: backdrop_pixels.py CAMERA_A.ppm CAMERA_B.ppm [ROUTE_LOG]", file=sys.stderr)
        return 2
    first_path, second_path = map(Path, argv[:2])
    width, height, first = read_ppm(first_path)
    second_width, second_height, second = read_ppm(second_path)
    if (second_width, second_height) != (width, height):
        print("FAIL: the two captures have different dimensions", file=sys.stderr)
        return 1

    if len(argv) == 2:
        metrics = measure(width, height, first, second)
        print(
            f"backdrop pixels: {width}x{height}, key holes={metrics.key_holes}, "
            f"edge join excess={metrics.far_join_excess}/{metrics.mountain_1100_join_excess}/"
            f"{metrics.mountain_1400_join_excess}, "
            f"camera-changed bytes={metrics.camera_changed_bytes}"
        )
        if any(metrics.__dict__.values()):
            print("FAIL: backdrop coverage, seam, or camera invariance contract was violated", file=sys.stderr)
            return 1
        return 0

    metrics = measure_scroll(width, height, first, second)
    trace = measure_trace(Path(argv[2]).read_text(errors="replace"))
    print(
        f"backdrop pixels: {width}x{height}, key holes={metrics.key_holes}, "
        f"edge join excess={metrics.far_join_excess}/{metrics.mountain_1100_join_excess}/"
        f"{metrics.mountain_1400_join_excess}, "
        f"static/scrolled changed bytes={metrics.static_changed_bytes}/{metrics.scrolling_changed_bytes}; "
        f"camera={trace.first_camera}/{trace.second_camera}, "
        f"draw={trace.first_draw_camera}/{trace.second_draw_camera}, "
        f"1400-offset={trace.first_mountain_offset}/{trace.second_mountain_offset}, "
        f"native rects={trace.main_blits}+{trace.continuation_blits}"
    )
    bad = (
        metrics.key_holes
        or metrics.far_join_excess
        or metrics.mountain_1100_join_excess
        or metrics.mountain_1400_join_excess
        or metrics.static_changed_bytes
        or not metrics.scrolling_changed_bytes
    )
    if bad:
        print("FAIL: scrolling backdrop coverage, seam, or native-geometry contract was violated", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
