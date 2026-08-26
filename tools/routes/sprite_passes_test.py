#!/usr/bin/env python3
"""Prove the object-sprite sampling chain does what it says on a real match frame (issue #112).

Seven arms of the SAME 3840x1975 match, differing only in ``LF2_SPRITE_PASSES``:

``base``      no chain at all -- the picture this engine has always drawn.
``identity``  ``nearest:2``. Magnifying the art by an integer and then sampling it on an
              integer grid selects the SAME texel for every fragment, so this frame must be
              BYTE-IDENTICAL to ``base``. That is the discriminator: it fails the moment the
              chain's coordinate walk drifts by a texel, and it caught exactly that -- taps
              addressed from the frame's uv origin instead of from a whole sheet texel
              resampled one sprite of the two while leaving the other alone.
``coarse``    ``nearest:1/2``. Half the resolution must CHANGE the picture; a chain that
              silently did nothing would pass the identity arm and fail here.
``aa``        ``aa``. Must change diagonal sprite contours on its own. This arm was missing
              when issue #113 was first declared resolved, so ``aa,outline:1`` could differ
              entirely because of the outline while edge smoothing contributed nothing.
``inner``     ``inner``. Must darken only edge-local pixels. It may not brighten a channel,
              alter a flat interior, or grow the silhouette into the exterior.
``aa_inner``  ``aa,inner``. Must differ from ``aa`` by the same inward-only treatment, proving
              that the contour composes with smoothing instead of replacing or disabling it.
``outline``   ``outline:1``. Must change the picture AND must paint pixels darker than
              anything the base frame had at those positions -- an outline that is merely
              "different" could be any bug at all.

Every arm reports its numbers whether it passes or fails: a route that prints nothing on the
negative cannot be told apart from one that never looked.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

from ppm import read_ppm

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_text

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "sprite_passes_test"
FRAME = "@match+282"
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
)
ARMS = (
    ("base", ""),
    ("identity", "nearest:2"),
    ("coarse", "nearest:1/2"),
    ("aa", "aa"),
    ("inner", "inner"),
    ("aa_inner", "aa,inner"),
    ("outline", "outline:1"),
)


def arm_environment(case: Path, passes: str) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items() if not key.startswith("LF2_")}
    env.update(
        SDL_AUDIODRIVER="dummy",
        SDL_VIDEODRIVER="offscreen",
        LF2_CONFIG="",
        LF2_UNPACED="1",
        LF2_ENGINE="1",
        LF2_WINDOW_SIZE="3840x1975",
        LF2_ENGINE_DEBUG="1",
        LF2_RENDER_DEBUG="1",
        # The lighting is off in every arm: this route is about SAMPLING, and shading the
        # frame as well would leave two reasons for a difference and no way to tell them apart.
        LF2_HD2D="off",
        LF2_VIRTUAL_PAD=",".join(PAD_ACTIONS),
        LF2_FRAME_DUMP=FRAME,
        LF2_DUMP_DIR=str(case),
        LF2_QUIT_AFTER="1460",
    )
    if passes:
        env["LF2_SPRITE_PASSES"] = passes
    return env


def run_arm(build: Path, game: Path, name: str, passes: str) -> tuple[Path, str] | None:
    case = OUTPUT / name
    case.mkdir(parents=True, exist_ok=True)
    log_path = case / "run.log"
    with log_path.open("w") as log:
        try:
            subprocess.run(
                [str(build / "lf2"), "lf2.exe"],
                cwd=game,
                env=arm_environment(case, passes),
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"  {name}: TIMED OUT after 300 s; see {log_path}")
            return None
    log_text = payload_text(log_path.read_text(errors="replace"))
    if "sprite passes:" in log_text:
        print(f"  {name}: the chain was REFUSED -- {log_text.split('sprite passes:')[1].splitlines()[0].strip()}")
        return None
    frames = sorted(case.glob("frame_*.ppm"))
    if len(frames) != 1:
        print(f"  {name}: {len(frames)} frame(s) dumped, expected 1; the route did not reach the match")
        return None
    return frames[0], log_text


def compare(a: Path, b: Path) -> tuple[int, int, int]:
    """max per-channel difference, differing pixels, total pixels."""
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"{a} is {wa}x{ha} but {b} is {wb}x{hb}")
    worst = differing = 0
    for i in range(0, len(pa), 3):
        delta = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
        if delta:
            differing += 1
            worst = max(worst, delta)
    return worst, differing, wa * ha


def darkened(base: Path, other: Path, floor: int = 24) -> int:
    """Pixels the second frame paints near-black where the first had something brighter."""
    _, _, pa = read_ppm(base)
    _, _, pb = read_ppm(other)
    count = 0
    for i in range(0, len(pa), 3):
        if max(pb[i], pb[i + 1], pb[i + 2]) <= floor < max(pa[i], pa[i + 1], pa[i + 2]):
            count += 1
    return count


def strictly_darker_pixels(base: Path, other: Path) -> tuple[int, int]:
    """Changed pixels that only lose channel energy, and changed pixels that do not.

    The inner contour mixes the sampled art toward black without touching alpha. On the final
    scene that can only reduce RGB channels. Any brightened changed pixel means this arm did
    something other than an inward dark contour.
    """
    _, _, pa = read_ppm(base)
    _, _, pb = read_ppm(other)
    darker = other_change = 0
    for i in range(0, len(pa), 3):
        a = pa[i : i + 3]
        b = pb[i : i + 3]
        if a == b:
            continue
        if all(b[channel] <= a[channel] for channel in range(3)) and any(
            b[channel] < a[channel] for channel in range(3)
        ):
            darker += 1
        else:
            other_change += 1
    return darker, other_change


def changed_pixels(base: Path, other: Path) -> set[int]:
    """Pixel indices changed between equal-sized frames."""
    width, height, pa = read_ppm(base)
    other_width, other_height, pb = read_ppm(other)
    if (width, height) != (other_width, other_height):
        raise ValueError("changed-mask inputs have different dimensions")
    return {pixel for pixel in range(width * height) if pa[pixel * 3 : pixel * 3 + 3] != pb[pixel * 3 : pixel * 3 + 3]}


def directionality_selftest() -> None:
    """Prove the inner/outer mask discriminator can report both answers."""
    inner = {10, 11, 12}
    outer = {20, 21, 22}
    if inner & outer:
        raise RuntimeError("directionality checker rejects disjoint synthetic inner/outer masks")
    outer.add(11)
    if len(inner & outer) != 1:
        raise RuntimeError("directionality checker cannot detect a synthetic exterior leak")


def changes_away_from_edges_pixels(
    width: int, height: int, pa: bytes, pb: bytes, radius: int = 2
) -> tuple[int, int]:
    changed = away = 0
    for pixel in range(width * height):
        offset = pixel * 3
        colour = pa[offset : offset + 3]
        if colour == pb[offset : offset + 3]:
            continue
        changed += 1
        x, y = pixel % width, pixel // width
        on_edge = False
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if (dx == 0 and dy == 0) or not (0 <= x + dx < width and 0 <= y + dy < height):
                    continue
                neighbour = ((y + dy) * width + x + dx) * 3
                if pa[neighbour : neighbour + 3] != colour:
                    on_edge = True
                    break
            if on_edge:
                break
        if not on_edge:
            away += 1
    return away, changed


def changes_away_from_edges(base: Path, other: Path, radius: int = 2) -> tuple[int, int]:
    """Changed pixels whose local neighbourhood was flat in the unfiltered frame.

    Edge reconstruction may touch silhouette or interior-colour contours, but it must not
    alter a pixel farther than half one magnified source cell from an authored output colour
    boundary. At the 3.591x route scale that bound is two output pixels. This persistent
    negative is what the former whole-sprite bilinear implementation failed.
    """
    width, height, pa = read_ppm(base)
    other_width, other_height, pb = read_ppm(other)
    if (width, height) != (other_width, other_height):
        raise ValueError("edge-locality inputs have different dimensions")
    return changes_away_from_edges_pixels(width, height, pa, pb, radius)


def edge_locality_selftest() -> None:
    """Prove the locality instrument can report both the allowed and forbidden answers."""
    width = height = 7
    flat = bytes((40, 50, 60)) * (width * height)
    changed_flat = bytearray(flat)
    centre = (3 * width + 3) * 3
    changed_flat[centre : centre + 3] = bytes((80, 90, 100))
    if changes_away_from_edges_pixels(width, height, flat, bytes(changed_flat)) != (1, 1):
        raise RuntimeError("edge-locality checker cannot detect a synthetic flat-interior change")

    edged = bytearray(flat)
    for y in range(height):
        for x in range(4, width):
            offset = (y * width + x) * 3
            edged[offset : offset + 3] = bytes((140, 150, 160))
    changed_edge = bytearray(edged)
    changed_edge[centre : centre + 3] = bytes((80, 90, 100))
    if changes_away_from_edges_pixels(width, height, bytes(edged), bytes(changed_edge)) != (0, 1):
        raise RuntimeError("edge-locality checker rejects a synthetic change beside an authored edge")


def main() -> int:
    build = (ROOT / os.environ.get("BUILD", "scratch/build-clang")).resolve()
    game = (ROOT / os.environ.get("GAME", "game")).resolve()
    if not (build / "lf2").is_file():
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    edge_locality_selftest()
    directionality_selftest()
    print("sprite passes: edge-locality selftest detects forbidden flat changes and allows edge changes")
    print("sprite passes: directionality selftest distinguishes disjoint contours from an exterior leak")
    print(f"sprite passes: {len(ARMS)} runs of the same match, one frame each ({FRAME})...")
    frames: dict[str, Path] = {}
    results: dict[str, tuple[Path, str]] = {}
    for name, passes in ARMS:
        result = run_arm(build, game, name, passes)
        if result is None:
            print(f"sprite passes: FAILED -- the {name} arm produced no frame to compare")
            return 1
        results[name] = result
        frames[name] = result[0]
        print(f"  {name}: {frames[name].name}  chain={passes or '(none)'}")

    failures: list[str] = []

    for name, (_, log_text) in results.items():
        if "engine: render targets are 3840x1975 output pixels" not in log_text:
            failures.append(f"{name}: the engine did not report a 3840x1975 render target")
        width, height, _ = read_ppm(frames[name])
        if (width, height) != (3840, 1975):
            failures.append(f"{name}: capture is {width}x{height}, not 3840x1975")

    worst, differing, total = compare(frames["base"], frames["identity"])
    print(f"  identity vs base: max {worst}, {differing} of {total} pixels differ")
    if differing:
        failures.append("nearest:2 changed the picture; the chain does not reproduce plain nearest")

    worst, differing, total = compare(frames["base"], frames["coarse"])
    print(f"  coarse   vs base: max {worst}, {differing} of {total} pixels differ")
    if differing < 200:
        failures.append(f"nearest:1/2 changed only {differing} pixels; halving the art did not happen")

    worst, differing, total = compare(frames["base"], frames["aa"])
    print(f"  aa       vs base: max {worst}, {differing} of {total} pixels differ")
    if differing < 100:
        failures.append(f"aa changed only {differing} pixels; diagonal edge reconstruction is inert")
    away, changed = changes_away_from_edges(frames["base"], frames["aa"])
    print(f"  aa edge locality: {away} of {changed} changed pixels were in flat 5x5 interiors")
    if away:
        failures.append(f"aa changed {away} pixels away from any authored edge; interiors did not stay exact")

    worst, differing, total = compare(frames["base"], frames["inner"])
    darker, other_change = strictly_darker_pixels(frames["base"], frames["inner"])
    print(
        f"  inner    vs base: max {worst}, {differing} of {total} pixels differ, "
        f"{darker} strictly darker, {other_change} other"
    )
    if differing < 100:
        failures.append(f"inner changed only {differing} pixels; no inner contour was drawn")
    if other_change:
        failures.append(f"inner changed {other_change} pixels without only darkening them")
    away, changed = changes_away_from_edges(frames["base"], frames["inner"])
    print(f"  inner edge locality: {away} of {changed} changed pixels were in flat 5x5 interiors")
    if away:
        failures.append(f"inner changed {away} pixels away from any authored edge; interiors did not stay exact")

    worst, differing, total = compare(frames["aa"], frames["aa_inner"])
    darker, other_change = strictly_darker_pixels(frames["aa"], frames["aa_inner"])
    print(
        f"  aa+inner vs aa: max {worst}, {differing} of {total} pixels differ, "
        f"{darker} strictly darker, {other_change} other"
    )
    if differing < 100:
        failures.append(f"aa,inner changed only {differing} pixels from aa; the contour did not compose with smoothing")
    if other_change:
        failures.append(f"aa,inner changed {other_change} pixels from aa without only darkening them")
    away, changed = changes_away_from_edges(frames["aa"], frames["aa_inner"])
    print(f"  aa+inner edge locality: {away} of {changed} changed pixels were in flat 5x5 aa interiors")
    if away:
        failures.append(f"aa,inner changed {away} pixels away from any smoothed edge")

    worst, differing, total = compare(frames["base"], frames["outline"])
    added = darkened(frames["base"], frames["outline"])
    print(f"  outline  vs base: max {worst}, {differing} of {total} pixels differ, {added} newly near-black")
    if differing < 100:
        failures.append(f"outline:1 changed only {differing} pixels; no border was drawn")
    if added < 100:
        failures.append(f"outline:1 painted only {added} near-black pixels; the border is not the outline")

    inner_mask = changed_pixels(frames["base"], frames["inner"])
    outline_mask = changed_pixels(frames["base"], frames["outline"])
    overlap = len(inner_mask & outline_mask)
    inner_only = len(inner_mask - outline_mask)
    outline_only = len(outline_mask - inner_mask)
    print(
        f"  contour direction: {inner_only} inner-only pixels, {outline_only} outer-only pixels, "
        f"{overlap} overlap"
    )
    # The outer arm grows and rerasterises a fractional quad, so its final RGB diff includes a
    # handful of authored edge pixels even though the shader composites black only under alpha.
    # Polarity is still decisive: a reused outer mask overlaps almost completely, while the
    # measured fractional-edge residue is 13/3320 (0.39%). Keep it below one percent and require
    # substantial exclusive evidence on both sides.
    if overlap * 100 > min(len(inner_mask), len(outline_mask)):
        failures.append(f"inner and outer contours overlap at {overlap} pixels; their polarity is not distinct")
    if inner_only < 100 or outline_only < 100:
        failures.append(
            f"contour direction has only {inner_only} inner-only and {outline_only} outer-only pixels"
        )

    if failures:
        print("sprite passes: FAILED")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print(
        "sprite passes: ok (4K target; identity exact; edge-only aa, inward contour, "
        "coarsening and outline changed the frame)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
