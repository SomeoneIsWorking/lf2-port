#!/usr/bin/env python3
"""Open the global RmlUi shell everywhere and verify the match transition pixels."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "ui_global_test"
RUN_LOG = OUTPUT / "run.log"

KEY = "0x1B@modemenu+20,0x1B@modemenu+60"
PAD = ",".join(
    (
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
)

# Pre-open, modal, and every frame around mapped Cancel. In the failing build the final frame
# below was the first frame with RmlUi hidden and had only 93.2% of the completed frame's
# non-black coverage because stale list lengths resurrected overwritten tile backing.
DUMP_SPEC = "@match+299,@match+305,@match+359,@match+360,@match+361,@match+362"


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def ppm_pixels(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    tokens: list[bytes] = []
    pos = 0
    while len(tokens) < 4:
        while pos < len(data) and data[pos] in b" \t\r\n":
            pos += 1
        if pos < len(data) and data[pos] == ord("#"):
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[pos:end])
        pos = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError(f"{path} is not an 8-bit binary PPM")
    while pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    width, height = int(tokens[1]), int(tokens[2])
    pixels = data[pos:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path} has {len(pixels)} pixel bytes, expected {width * height * 3}")
    return width, height, pixels


def coverage(pixels: bytes) -> float:
    non_black = sum(
        pixels[offset] != 0 or pixels[offset + 1] != 0 or pixels[offset + 2] != 0
        for offset in range(0, len(pixels), 3)
    )
    return non_black / (len(pixels) // 3)


def changed_fraction(left: bytes, right: bytes) -> float:
    if len(left) != len(right):
        return 1.0
    changed = sum(left[offset : offset + 3] != right[offset : offset + 3]
                  for offset in range(0, len(left), 3))
    return changed / (len(left) // 3)


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

    OUTPUT.mkdir(parents=True, exist_ok=True)
    for old in OUTPUT.glob("frame_*.ppm"):
        old.unlink()
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_WINDOW_SIZE="1920x1080",
        LF2_KEY_SCRIPT=KEY,
        LF2_VIRTUAL_PAD=PAD,
        LF2_RMLUI_DEBUG="1",
        LF2_FRAME_DUMP=DUMP_SPEC,
        LF2_DUMP_DIR=str(OUTPUT),
        LF2_QUIT_AFTER="2450",
    )

    print("global RmlUi: opening one shell on every screen and checking match transitions...", flush=True)
    with RUN_LOG.open("w") as output:
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
            print("  FAIL  the route exceeded 300 seconds")
            return 1

    text = RUN_LOG.read_text(errors="replace")
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

    frames = sorted(OUTPUT.glob("frame_*.ppm"))
    checks.append((len(frames) == 6, f"all six transition frames were captured ({len(frames)})"))
    if len(frames) == 6:
        decoded = [ppm_pixels(frame) for frame in frames]
        checks.append((all(width == 1920 and height == 1080 for width, height, _ in decoded),
                       "GPU readback retained the 1920x1080 output resolution"))
        before = decoded[0][2]
        modal = decoded[1][2]
        closed = decoded[-1][2]
        before_coverage = coverage(before)
        closed_coverage = coverage(closed)
        checks.append((changed_fraction(before, modal) >= 0.05,
                       "the captured modal frame visibly differs from the live frame"))
        checks.append((changed_fraction(modal, closed) >= 0.05,
                       "mapped Cancel removed the modal in the captured close frame"))
        checks.append((closed_coverage >= before_coverage * 0.98,
                       f"the first closed frame kept completed-frame coverage "
                       f"({closed_coverage:.4f} vs {before_coverage:.4f})"))

    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    print(f"global RmlUi test {'FAILED' if failed else 'PASSED'}")
    if failed:
        print(f"        log and transition frames: {OUTPUT}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
