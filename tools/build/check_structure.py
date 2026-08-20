#!/usr/bin/env python3
"""Reject new runtime monoliths and growth of the legacy ones."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LIMIT = 500
LEGACY_LIMITS = {
    "runtime/video/ddraw.c": 2612,
    "runtime/win32/imports.c": 1293,
    "runtime/video/engine.c": 1282,
    "runtime/video/render.c": 1183,
    "runtime/win32/win32.c": 1126,
    "runtime/win32/gdi.c": 1071,
    "runtime/overrides/background.c": 925,
    "runtime/overrides/coop_debug.c": 798,
    "runtime/video/mesh.c": 750,
    "runtime/audio/dsound.c": 727,
    "runtime/overrides/coop.c": 613,
    "runtime/overrides/screens.c": 561,
    "runtime/cpu/guest.c": 503,
}


def line_count(path: Path) -> int:
    with path.open("rb") as source:
        return sum(1 for _ in source)


def main() -> int:
    failures = []
    for path in sorted((ROOT / "runtime").rglob("*")):
        if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        relative = path.relative_to(ROOT).as_posix()
        limit = LEGACY_LIMITS.get(relative, DEFAULT_LIMIT)
        measured = line_count(path)
        if measured > limit:
            failures.append(f"{relative}: {measured} lines (limit {limit})")

    retired = [ROOT / "runtime/app/rmlui.cpp", ROOT / "runtime/app/rmlui.h"]
    for path in retired:
        if path.exists():
            failures.append(f"{path.relative_to(ROOT)}: retired UI god-file path returned")

    if failures:
        print("structure: FAILED")
        for failure in failures:
            print(f"  {failure}")
        print("Split by responsibility; never raise a limit merely to land a feature.")
        return 1
    print(f"structure: ok (new runtime files <= {DEFAULT_LIMIT} lines; legacy files did not grow)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
