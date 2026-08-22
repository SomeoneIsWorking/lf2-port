#!/usr/bin/env python3
"""Measure real LF2 fighter feet against their projected silhouettes (issue #97)."""

from __future__ import annotations

from collections import deque
import os
from pathlib import Path
import subprocess

from ppm import read_ppm


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "shadow_contact_test"
FRAMES = ",".join(f"@match+{offset}" for offset in range(120, 601, 60))
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
        "up@overlay+99",
        "up@overlay+159",
        "south@overlay+219",
        "right@match+108",
        "south@match+158",
    )
)


def mask_pixels(width: int, height: int, pixels: bytes) -> set[tuple[int, int]]:
    result: set[tuple[int, int]] = set()
    for y in range(height):
        row = y * width * 3
        for x in range(width):
            offset = row + x * 3
            if pixels[offset] >= 240:
                result.add((x, y))
    return result


def components(mask: set[tuple[int, int]]) -> list[set[tuple[int, int]]]:
    remaining = set(mask)
    found: list[set[tuple[int, int]]] = []
    while remaining:
        seed = remaining.pop()
        piece = {seed}
        queue = deque((seed,))
        while queue:
            x, y = queue.popleft()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    neighbour = (x + dx, y + dy)
                    if neighbour in remaining:
                        remaining.remove(neighbour)
                        piece.add(neighbour)
                        queue.append(neighbour)
        found.append(piece)
    return found


def bounds(piece: set[tuple[int, int]]) -> tuple[int, int, int, int]:
    xs = [point[0] for point in piece]
    ys = [point[1] for point in piece]
    return min(xs), min(ys), max(xs), max(ys)


def contact_gap(fighter: set[tuple[int, int]], shadow: set[tuple[int, int]], limit: int = 8) -> int:
    _left, top, _right, bottom = bounds(fighter)
    foot_band = {point for point in fighter if point[1] >= bottom - max(4, (bottom - top + 1) // 4)}
    for gap in range(limit + 1):
        for x, y in foot_band:
            for dy in range(-gap, gap + 1):
                if (x - gap, y + dy) in shadow or (x + gap, y + dy) in shadow:
                    return gap
            for dx in range(-gap + 1, gap):
                if (x + dx, y - gap) in shadow or (x + dx, y + gap) in shadow:
                    return gap
    return limit + 1


def analyser_selftest() -> None:
    fighter = {(2, 1), (2, 2)}
    touching = {(3, 2)}
    detached = {(9, 2)}
    if contact_gap(fighter, touching) != 1:
        raise RuntimeError("contact analyser rejects a touching synthetic silhouette")
    if contact_gap(fighter, detached) <= 2:
        raise RuntimeError("contact analyser cannot detect the former seven-pixel detachment")


def run_arm(binary: Path, game: Path, show: str) -> list[Path] | None:
    case = OUTPUT / show
    case.mkdir(parents=True, exist_ok=True)
    for old in case.glob("frame_*.ppm"):
        old.unlink()
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE="794x550",
        LF2_ENGINE="1",
        LF2_HD2D="on",
        LF2_HD2D_SHOW=show,
        LF2_HD2D_LIGHT="-48.7,70",
        LF2_VIRTUAL_PAD=PAD,
        LF2_FRAME_DUMP=FRAMES,
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1800",
    )
    log = case / "run.log"
    print(f"  {show} mask...", flush=True)
    with log.open("w") as output:
        try:
            result = subprocess.run(
                [str(binary), "lf2.exe"],
                cwd=game,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=240,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"    FAIL  {show} arm exceeded 240 seconds")
            return None
    frames = sorted(case.glob("frame_*.ppm"))
    if result.returncode != 0 or len(frames) != 9:
        print(
            f"    FAIL  status {result.returncode}, expected nine frames and found {len(frames)}; "
            f"see {log}"
        )
        return None
    return frames


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    binary = build / "lf2"
    if not binary.is_file():
        print(f"SKIP: {binary} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    analyser_selftest()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    print("shadow contact: two real LF2 fighter silhouettes across deterministic match frames")
    char_frames = run_arm(binary, game, "chars")
    shadow_frames = run_arm(binary, game, "shadow")
    if char_frames is None or shadow_frames is None:
        return 1
    if len(char_frames) != len(shadow_frames):
        print(f"  FAIL  capture counts differ: chars {len(char_frames)}, shadow {len(shadow_frames)}")
        return 1

    for char_frame, shadow_frame in zip(char_frames, shadow_frames, strict=True):
        cw, ch, char_rgb = read_ppm(char_frame)
        sw, sh, shadow_rgb = read_ppm(shadow_frame)
        if (cw, ch) != (sw, sh):
            print(f"  FAIL  mask dimensions differ: chars {cw}x{ch}, shadow {sw}x{sh}")
            return 1
        fighters = [
            piece
            for piece in components(mask_pixels(cw, ch, char_rgb))
            if len(piece) >= 40 and bounds(piece)[3] - bounds(piece)[1] + 1 >= 12
        ]
        fighters.sort(key=len, reverse=True)
        if len(fighters) < 2:
            print(f"  {char_frame.name}: only {len(fighters)} fighter-sized silhouette(s)")
            continue
        fighters = sorted(fighters[:2], key=lambda piece: bounds(piece)[0])
        normalised = {
            frozenset((x - bounds(piece)[0], y - bounds(piece)[1]) for x, y in piece)
            for piece in fighters
        }
        shadow = mask_pixels(sw, sh, shadow_rgb)
        gaps = [contact_gap(fighter, shadow) for fighter in fighters]
        descriptions = [
            f"fighter {number} bbox={bounds(fighter)} opaque={len(fighter)} gap={gap}px"
            for number, (fighter, gap) in enumerate(zip(fighters, gaps, strict=True), 1)
        ]
        print(f"  {char_frame.name}: {'; '.join(descriptions)}")
        if len(normalised) == 2 and all(gap <= 2 for gap in gaps):
            print("shadow contact: PASS -- both distinct real silhouettes meet within 2px")
            print(f"  captures: {char_frame} and {shadow_frame}")
            print(
                "  negative: issue #72 traced old ellipse recentering at 7px and 12px; "
                "analyser selftest rejects 7px"
            )
            return 0

    print("shadow contact: FAIL -- no captured frame had two distinct grounded silhouettes")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
