#!/usr/bin/env python3
"""Authenticate issue #84's native Background and Stage pre-fight labels at 3840x1975."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess

from ppm import read_ppm


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "overlay_labels_test"
PAD = ",".join(
    (
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
    )
)
STATIC_LABEL_ROWS = ((16, 39), (39, 64), (64, 87), (137, 163))
LOGICAL_PANEL_WIDTH = 304
LOGICAL_PANEL_HEIGHT = 166
LOGICAL_VALUE_LABEL_RIGHT = 170
OUTPUT_PANEL_WIDTH = 1092
OUTPUT_PANEL_HEIGHT = 596
PANEL_RGB = (57, 87, 173)
CJK_GLYPH_WIDTH = 47
EXPECTED_CJK_RECTANGLES = [
    (564, 57, 94, 62),
    (503, 143, 282, 62),
    (568, 229, 282, 62),
    (487, 314, 94, 62),
    (487, 404, 94, 62),
    (544, 497, 94, 62),
]


def is_native_label_pixel(red: int, green: int, blue: int) -> bool:
    return red >= 100 and green >= 130 and blue >= 200


def static_label_counts(width: int, pixels: bytes) -> list[int]:
    scale = 1975 / 550
    screen_offset = (1070 - 794) // 2
    left = int(-1 + (screen_offset + 15) * scale)
    right = int(-1 + (screen_offset + 295) * scale)
    counts: list[int] = []
    for row_top, row_bottom in STATIC_LABEL_ROWS:
        top = int(row_top * scale)
        bottom = int(row_bottom * scale)
        count = 0
        for y in range(top, bottom):
            for x in range(left, right):
                offset = (y * width + x) * 3
                count += is_native_label_pixel(*pixels[offset : offset + 3])
        counts.append(count)
    return counts


def label_signature_selftest() -> None:
    if is_native_label_pixel(57, 87, 173) or is_native_label_pixel(83, 111, 192):
        raise RuntimeError("native-label classifier mistakes panel blue for glyph coverage")
    if not is_native_label_pixel(159, 190, 255):
        raise RuntimeError("native-label classifier rejects the authored unselected ink")


def crop_rgb(width: int, pixels: bytes, left: int, top: int, crop_width: int, crop_height: int) -> bytearray:
    crop = bytearray(crop_width * crop_height * 3)
    for y in range(crop_height):
        source = ((top + y) * width + left) * 3
        destination = y * crop_width * 3
        crop[destination : destination + crop_width * 3] = pixels[source : source + crop_width * 3]
    return crop


def label_coverage_counts(
    panel: bytes | bytearray, panel_width: int, rectangles: list[tuple[int, int, int, int]]
) -> list[int]:
    counts: list[int] = []
    for left, top, width, height in rectangles:
        count = 0
        for y in range(top, top + height):
            for x in range(left, left + width):
                offset = (y * panel_width + x) * 3
                count += is_native_label_pixel(*panel[offset : offset + 3])
        counts.append(count)
    return counts


def blank_rectangles(
    panel: bytearray, panel_width: int, rectangles: list[tuple[int, int, int, int]]
) -> None:
    for left, top, width, height in rectangles:
        for y in range(top, top + height):
            for x in range(left, left + width):
                offset = (y * panel_width + x) * 3
                panel[offset : offset + 3] = bytes(PANEL_RGB)


def copy_rectangles(
    destination: bytearray,
    source: bytes | bytearray,
    panel_width: int,
    rectangles: list[tuple[int, int, int, int]],
) -> None:
    for left, top, width, height in rectangles:
        for y in range(top, top + height):
            begin = (y * panel_width + left) * 3
            end = begin + width * 3
            destination[begin:end] = source[begin:end]


def nearest_logical_negative(panel: bytes | bytearray, width: int, height: int) -> bytearray:
    logical = bytearray(LOGICAL_PANEL_WIDTH * LOGICAL_PANEL_HEIGHT * 3)
    for y in range(LOGICAL_PANEL_HEIGHT):
        source_y = min(height - 1, ((2 * y + 1) * height) // (2 * LOGICAL_PANEL_HEIGHT))
        for x in range(LOGICAL_PANEL_WIDTH):
            source_x = min(width - 1, ((2 * x + 1) * width) // (2 * LOGICAL_PANEL_WIDTH))
            source = (source_y * width + source_x) * 3
            destination = (y * LOGICAL_PANEL_WIDTH + x) * 3
            logical[destination : destination + 3] = panel[source : source + 3]

    nearest = bytearray(width * height * 3)
    for y in range(height):
        source_y = y * LOGICAL_PANEL_HEIGHT // height
        for x in range(width):
            source_x = x * LOGICAL_PANEL_WIDTH // width
            source = (source_y * LOGICAL_PANEL_WIDTH + source_x) * 3
            destination = (y * width + x) * 3
            nearest[destination : destination + 3] = logical[source : source + 3]
    return nearest


def within_logical_cell_edge_score(
    panel: bytes | bytearray, width: int, height: int, rectangles: list[tuple[int, int, int, int]]
) -> int:
    score = 0
    for left, top, rect_width, rect_height in rectangles:
        for y in range(top, top + rect_height):
            for x in range(left, left + rect_width):
                offset = (y * width + x) * 3
                pixel = panel[offset : offset + 3]
                if x + 1 < left + rect_width and x * LOGICAL_PANEL_WIDTH // width == (
                    (x + 1) * LOGICAL_PANEL_WIDTH // width
                ):
                    neighbour = panel[offset + 3 : offset + 6]
                    delta = sum(abs(pixel[channel] - neighbour[channel]) for channel in range(3))
                    if delta >= 8 and (is_native_label_pixel(*pixel) or is_native_label_pixel(*neighbour)):
                        score += 1
                if y + 1 < top + rect_height and y * LOGICAL_PANEL_HEIGHT // height == (
                    (y + 1) * LOGICAL_PANEL_HEIGHT // height
                ):
                    below_offset = offset + width * 3
                    neighbour = panel[below_offset : below_offset + 3]
                    delta = sum(abs(pixel[channel] - neighbour[channel]) for channel in range(3))
                    if delta >= 8 and (is_native_label_pixel(*pixel) or is_native_label_pixel(*neighbour)):
                        score += 1
    return score


def cjk_rectangles(log: str) -> list[tuple[int, int, int, int]] | None:
    report = re.search(
        r"^overlay panel CJK output rectangles:" + "".join(rf" {row}=(\d+),(\d+),(\d+),(\d+)" for row in range(6)) + r"$",
        log,
        re.MULTILINE,
    )
    if report is None:
        return None
    values = list(map(int, report.groups()))
    return [tuple(values[index : index + 4]) for index in range(0, len(values), 4)]


def cjk_glyph_cells(rectangles: list[tuple[int, int, int, int]]) -> list[tuple[int, int, int, int]]:
    cells: list[tuple[int, int, int, int]] = []
    for left, top, width, height in rectangles:
        for glyph in range(width // CJK_GLYPH_WIDTH):
            cells.append((left + glyph * CJK_GLYPH_WIDTH, top, CJK_GLYPH_WIDTH, height))
    return cells


def latin_segments(rectangles: list[tuple[int, int, int, int]]) -> list[tuple[int, int, int, int]]:
    value_label_right = round(LOGICAL_VALUE_LABEL_RIGHT * OUTPUT_PANEL_HEIGHT / LOGICAL_PANEL_HEIGHT)
    segments: list[tuple[int, int, int, int]] = []
    for row, (left, top, width, height) in enumerate(rectangles):
        right_edge = value_label_right if row in (3, 4) else OUTPUT_PANEL_WIDTH
        segments.extend(((0, top, left, height), (left + width, top, right_edge - left - width, height)))
    return segments


def rectangles_in_bounds(rectangles: list[tuple[int, int, int, int]], width: int, height: int) -> bool:
    return all(
        left >= 0
        and top >= 0
        and rect_width > 0
        and rect_height > 0
        and left + rect_width <= width
        and top + rect_height <= height
        for left, top, rect_width, rect_height in rectangles
    )


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def run_case(
    binary: Path, game: Path, case_name: str, game_mode: str, force_failure: bool = False
) -> tuple[int, str, Path] | None:
    case = OUTPUT / case_name
    case.mkdir(parents=True, exist_ok=True)
    for old in case.glob("frame_*.ppm"):
        old.unlink()
    log = case / "run.log"
    env = {key: value for key, value in os.environ.items() if not key.startswith("LF2_")}
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_ENGINE="1",
        LF2_HD2D="1",
        LF2_RENDERER="gpu",
        LF2_WINDOW_SIZE="3840x1975",
        LF2_MODE=game_mode,
        LF2_OVERLAY_FORCE="3",
        LF2_VIRTUAL_PAD=PAD,
        LF2_FRAME_DUMP="@overlay+80",
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1100",
        LF2_OVERLAY_PANEL_DEBUG="1",
        LF2_TEXT_DEBUG="1",
        LF2_RENDER_DEBUG="1",
        LF2_ENGINE_DEBUG="1",
    )
    if force_failure:
        env["LF2_OVERLAY_PANEL_FORCE_FAILURE"] = "1"
    with log.open("w") as output:
        process = subprocess.Popen(
            [str(binary), "lf2.exe"],
            cwd=game,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
        print(f"  launched {case_name} game PID {process.pid}", flush=True)
        try:
            returncode = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print(f"  FAIL  {case_name} exceeded 180 seconds")
            return None
    frames = sorted(case.glob("frame_*.ppm"))
    if len(frames) != 1:
        print(f"  FAIL  {case_name} captured {len(frames)} frames, expected one")
        return None
    return returncode, log.read_text(errors="replace"), frames[0]


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

    label_signature_selftest()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("overlay labels: exact-output native panel in Background and Stage modes...", flush=True)
    runs = []
    for case_name, game_mode, force_failure in (("vs", "vs", False), ("stage", "stage", False), ("fallback", "vs", True)):
        runs.append((case_name, run_case(binary, game, case_name, game_mode, force_failure)))
    failed = False
    for mode, run in runs:
        if run is None:
            failed = True
            continue
        returncode, log, frame = run
        label = "Stage" if mode == "stage" else "Background"
        if returncode != 0:
            print(f"  FAIL  {mode} exited with status {returncode}")
            failed = True
        for evidence, description in (
            ("LF2_VIRTUAL_PAD: 10 of 10 items fired", "all ten route actions"),
            ("engine: render targets are 3840x1975 output pixels", "3840x1975 engine target"),
        ):
            if evidence not in log:
                print(f"  FAIL  {mode} lacks {description}")
                failed = True
        dynamic_pattern = (
            r"^text \(312,91\).* Random$"
            if mode != "stage"
            else r"^text \(332,91\).* (?:[1-5]|Survival)$"
        )
        if re.search(dynamic_pattern, log, re.MULTILINE) is None:
            print(f"  FAIL  {mode} lacks the game's expected dynamic {label} value")
            failed = True
        if re.search(r"^text \(312,115\).* Difficult$", log, re.MULTILINE) is None:
            print(f"  FAIL  {mode} lacks the game's dynamic Difficulty value")
            failed = True
        report = re.search(
            rf"overlay panel: (\d+) native panel\(s\) appended after (\d+) final authored part\(s\); "
            rf"originals retained; (\d+) forced failure\(s\); output raster (\d+)x(\d+); selected 3; {label} label",
            log,
        )
        if report is None:
            print(f"  FAIL  {mode} lacks the authenticated {label} panel report")
            failed = True
        else:
            appended, final_parts, forced_failures, width, height = map(int, report.groups())
            expected_appended = 0 if mode == "fallback" else final_parts
            expected_failures = final_parts if mode == "fallback" else 0
            expected_raster = (0, 0) if mode == "fallback" else (OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT)
            if final_parts <= 0 or appended != expected_appended or forced_failures != expected_failures or (
                width,
                height,
            ) != expected_raster:
                print(
                    f"  FAIL  {mode} report has appended={appended}, final_parts={final_parts}, "
                    f"forced_failures={forced_failures}, raster={width}x{height}"
                )
                failed = True
            else:
                print(
                    f"  ok    {mode}: {appended} panels, {final_parts} retained final parts, "
                    f"{forced_failures} forced failures, {width}x{height} {label} raster"
                )
        render_report = re.search(r"^render: gpu=on .* dropped=(\d+) ", log, re.MULTILINE)
        if render_report is None or render_report.group(1) != "0":
            print(f"  FAIL  {mode} renderer did not authenticate dropped=0")
            failed = True
        texture_report = re.search(r"^engine textures: .* (\d+) request\(s\) failed$", log, re.MULTILINE)
        if texture_report is None or texture_report.group(1) != "0":
            print(f"  FAIL  {mode} texture cache did not authenticate zero failed requests")
            failed = True
        failure_markers = ("tile arena exhausted", "texture pool exhausted", "upload failed")
        if any(marker in log.lower() for marker in failure_markers):
            print(f"  FAIL  {mode} reported a pool, tile, or upload failure")
            failed = True
        if "overlay panel: embedded outline face failed" in log:
            print(f"  FAIL  {mode} could not render an embedded outline face")
            failed = True
        try:
            width, height, pixels = read_ppm(frame)
        except ValueError as error:
            print(f"  FAIL  {mode}: {error}")
            failed = True
            continue
        if (width, height) != (3840, 1975):
            print(f"  FAIL  {mode} capture is {width}x{height}")
            failed = True
            continue
        label_counts = static_label_counts(width, pixels)
        minimum_label_pixels = 100 if mode == "fallback" else 1000
        if any(count < minimum_label_pixels for count in label_counts):
            print(f"  FAIL  {mode} static native-label coverage is missing: {label_counts}")
            failed = True
        else:
            description = "retained original-label" if mode == "fallback" else "static native-label"
            print(f"  ok    {mode}: {description} coverage by row is {label_counts}")
        if mode != "fallback":
            rectangles = cjk_rectangles(log)
            if rectangles is None:
                print(f"  FAIL  {mode} lacks exact output CJK rectangles")
                failed = True
            elif not rectangles_in_bounds(rectangles, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT):
                print(f"  FAIL  {mode} output CJK rectangles escape the authenticated panel: {rectangles}")
                failed = True
            elif rectangles != EXPECTED_CJK_RECTANGLES:
                print(f"  FAIL  {mode} output CJK rectangles are {rectangles}, expected {EXPECTED_CJK_RECTANGLES}")
                failed = True
            else:
                scale = 1975 / 550
                panel_left = int(-1 + (138 + 3) * scale)
                panel_top = int(3 * scale)
                panel = crop_rgb(width, pixels, panel_left, panel_top, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT)
                coverage = label_coverage_counts(panel, OUTPUT_PANEL_WIDTH, rectangles)
                if any(count < 20 for count in coverage):
                    print(f"  FAIL  {mode} CJK-run coverage is missing: {coverage}")
                    failed = True
                else:
                    print(f"  ok    {mode}: exact CJK-run coverage by row is {coverage}")

                blanked = bytearray(panel)
                blank_rectangles(blanked, OUTPUT_PANEL_WIDTH, rectangles)
                blanked_coverage = label_coverage_counts(blanked, OUTPUT_PANEL_WIDTH, rectangles)
                if any(blanked_coverage):
                    print(f"  FAIL  {mode} CJK mutation negative was not rejected: {blanked_coverage}")
                    failed = True

                glyph_cells = cjk_glyph_cells(rectangles)
                glyph_coverage = label_coverage_counts(panel, OUTPUT_PANEL_WIDTH, glyph_cells)
                glyph_scores = [
                    within_logical_cell_edge_score(panel, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT, [cell])
                    for cell in glyph_cells
                ]
                nearest = nearest_logical_negative(panel, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT)
                nearest_coverage = label_coverage_counts(nearest, OUTPUT_PANEL_WIDTH, glyph_cells)
                nearest_scores = [
                    within_logical_cell_edge_score(nearest, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT, [cell])
                    for cell in glyph_cells
                ]
                if (
                    len(glyph_cells) != 20
                    or any(count < 300 for count in glyph_coverage)
                    or any(score < 250 for score in glyph_scores)
                    or any(count < 300 for count in nearest_coverage)
                    or any(nearest_scores)
                ):
                    print(
                        f"  FAIL  {mode} full-resolution glyph discriminator has coverage={glyph_coverage}, "
                        f"edges={glyph_scores}, logical-nearest coverage={nearest_coverage}, edges={nearest_scores}"
                    )
                    failed = True
                else:
                    print(
                        f"  ok    {mode}: all 20 CJK glyph cells have coverage {glyph_coverage} and "
                        f"full-resolution edge scores {glyph_scores}; synthesized logical-nearest coverage "
                        f"{nearest_coverage} with zero per-glyph edge scores"
                    )

                segments = latin_segments(rectangles)
                latin_coverage = label_coverage_counts(panel, OUTPUT_PANEL_WIDTH, segments)
                latin_scores = [
                    within_logical_cell_edge_score(panel, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT, [segment])
                    for segment in segments
                ]
                latin_minimums = [minimum for _ in range(6) for minimum in (500, 100)]

                hybrid = bytearray(nearest)
                copy_rectangles(hybrid, panel, OUTPUT_PANEL_WIDTH, rectangles)
                hybrid_cjk_coverage = label_coverage_counts(hybrid, OUTPUT_PANEL_WIDTH, glyph_cells)
                hybrid_cjk_scores = [
                    within_logical_cell_edge_score(hybrid, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT, [cell])
                    for cell in glyph_cells
                ]
                hybrid_latin_coverage = label_coverage_counts(hybrid, OUTPUT_PANEL_WIDTH, segments)
                hybrid_latin_scores = [
                    within_logical_cell_edge_score(hybrid, OUTPUT_PANEL_WIDTH, OUTPUT_PANEL_HEIGHT, [segment])
                    for segment in segments
                ]
                if (
                    any(count < minimum for count, minimum in zip(latin_coverage, latin_minimums, strict=True))
                    or any(score < minimum for score, minimum in zip(latin_scores, latin_minimums, strict=True))
                    or any(count < 300 for count in hybrid_cjk_coverage)
                    or any(score < 250 for score in hybrid_cjk_scores)
                    or any(
                        count < minimum
                        for count, minimum in zip(hybrid_latin_coverage, latin_minimums, strict=True)
                    )
                    or any(hybrid_latin_scores)
                ):
                    print(
                        f"  FAIL  {mode} full-resolution Latin discriminator has coverage={latin_coverage}, "
                        f"edges={latin_scores}; native-CJK/nearest-Latin hybrid has CJK "
                        f"coverage={hybrid_cjk_coverage}, edges={hybrid_cjk_scores}, Latin "
                        f"coverage={hybrid_latin_coverage}, edges={hybrid_latin_scores}"
                    )
                    failed = True
                else:
                    print(
                        f"  ok    {mode}: all Latin before/after runs have coverage {latin_coverage} and "
                        f"full-resolution edge scores {latin_scores}; native-CJK/nearest-Latin hybrid "
                        "retains glyph coverage but has zero Latin edge scores"
                    )
        if mode != "fallback":
            scale = 1975 / 550
            well_x = int(-1 + (138 + 280 + 0.5) * scale)
            for logical_y, description in ((99, label), (123, "Difficulty")):
                well_y = int((logical_y + 0.5) * scale)
                offset = (well_y * width + well_x) * 3
                if pixels[offset : offset + 3] != b"\0\0\0":
                    print(f"  FAIL  {mode} {description} value well is absent or misplaced")
                    failed = True
                else:
                    print(f"  ok    {mode}: {description} value well is present at the authored anchor")

    print(f"overlay-label test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
