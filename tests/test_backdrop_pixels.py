#!/usr/bin/env python3
"""Negative controls for the Lion Forest capture pixel gate."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "routes"))

from backdrop_pixels import measure, measure_scroll, measure_trace  # noqa: E402


def synthetic_trace(second_camera: int = 300, scaled_main: bool = False, scaled_native: bool = False) -> str:
    lines: list[str] = []
    for frame, camera, offset in ((100, 0, 0), (200, second_camera, -20 if second_camera else 0)):
        lines.append(f"backdrop camera frame {frame} guest={camera} draw={camera} view=1302 stage=2900")
        for index, span, x in ((0, 800, 0), (1, 1100, 0), (2, 1100, 800), (3, 1400, 0), (4, 1400, 1216)):
            layer_offset = offset if span == 1400 else 0
            lines.append(
                f"backdrop layer frame {frame} index={index} span={span} x={x} "
                f"off={layer_offset} flags={1 if index in (0, 2, 4) else 0}"
            )
            destination_width = 99 if scaled_main and frame == 200 and index == 4 else 100
            lines.append(
                f"blt 1 dst=1[1302x550] rect=(0,128)-({destination_width},198) "
                "src=2[100x70] srect=(0,0)-(100,70) flags=0 from=0043f178"
            )
        native_width = 99 if scaled_native and frame == 200 else 100
        lines.append(
            f"backdrop native frame {frame} mirror=1 dst=(800,128)-({800 + native_width},198) "
            "src=(0,0)-(100,70)"
        )
    return "\n".join(lines)


def synthetic_frame(width: int = 1600, height: int = 550) -> bytearray:
    pixels = bytearray([31, 47, 63]) * (width * height)
    far_left = 0
    far_right = 800
    for y in range(128, 198):
        left = (80 + y % 7, 100, 120)
        right = (120, 130 + y % 5, 140)
        for x in range(far_left, far_right):
            t = (x - far_left) * 40 // 799
            offset = (y * width + x) * 3
            pixels[offset : offset + 3] = bytes((left[0] + t, left[1] + t * 3 // 4, left[2] + t // 2))
        for x in range(far_right, width):
            source_x = far_right - 1 - (x - far_right)
            offset = (y * width + x) * 3
            source = (y * width + source_x) * 3
            pixels[offset : offset + 3] = pixels[source : source + 3]

    def reflected_piece(left: int, piece_width: int, top: int) -> None:
        right = left + piece_width
        for y in range(top, 198):
            for x in range(left, right):
                offset = (y * width + x) * 3
                pixels[offset : offset + 3] = bytes((40 + (x - left) % 70, 75 + y % 20, 90))
            for x in range(right, width):
                distance = x - right
                phase = distance % (2 * piece_width)
                source_x = right - 1 - phase if phase < piece_width else left + phase - piece_width
                offset = (y * width + x) * 3
                source = (y * width + source_x) * 3
                pixels[offset : offset + 3] = pixels[source : source + 3]

    reflected_piece(800, 300, 147)
    reflected_piece(1216, 184, 155)
    return pixels


def main() -> int:
    width, height = 1600, 550
    clean = synthetic_frame(width, height)
    assert measure(width, height, clean, clean).__dict__ == {
        "key_holes": 0,
        "far_join_excess": 0,
        "mountain_1100_join_excess": 0,
        "mountain_1400_join_excess": 0,
        "camera_changed_bytes": 0,
    }

    hole = bytearray(clean)
    hole[(150 * width + 20) * 3 : (150 * width + 20) * 3 + 3] = b"\0\0\0"
    assert measure(width, height, hole, hole).key_holes == 1

    seam = bytearray(clean)
    far_right = 800
    for y in range(128, 147):
        seam[(y * width + far_right) * 3 : (y * width + far_right) * 3 + 3] = b"\xff\xff\xff"
    assert measure(width, height, seam, seam).far_join_excess > 0

    seam_1100 = bytearray(clean)
    for y in range(147, 198):
        seam_1100[(y * width + 1100) * 3 : (y * width + 1100) * 3 + 3] = b"\xff\xff\xff"
    assert measure(width, height, seam_1100, seam_1100).mountain_1100_join_excess > 0

    seam_1400 = bytearray(clean)
    for y in range(155, 198):
        seam_1400[(y * width + 1400) * 3 : (y * width + 1400) * 3 + 3] = b"\xff\xff\xff"
    assert measure(width, height, seam_1400, seam_1400).mountain_1400_join_excess > 0

    moved = bytearray(clean)
    moved[(160 * width + 700) * 3] ^= 1
    assert measure(width, height, clean, moved).camera_changed_bytes == 1
    scrolled = bytearray(clean)
    scrolled[(160 * width + 700) * 3] ^= 1
    scroll_metrics = measure_scroll(width, height, clean, scrolled)
    assert scroll_metrics.static_changed_bytes == 0
    assert scroll_metrics.scrolling_changed_bytes == 1
    static_mutation = bytearray(clean)
    static_mutation[(130 * width + 700) * 3] ^= 1
    assert measure_scroll(width, height, clean, static_mutation).static_changed_bytes == 1
    trace = measure_trace(synthetic_trace())
    assert (trace.first_camera, trace.second_camera) == (0, 300)
    assert (trace.first_mountain_offset, trace.second_mountain_offset) == (0, -20)
    for bad in (
        synthetic_trace(second_camera=0),
        synthetic_trace(scaled_main=True),
        synthetic_trace(scaled_native=True),
    ):
        try:
            measure_trace(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("trace negative control was accepted")
    print("backdrop pixels: positive and nine negative controls passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
