#!/usr/bin/env python3
"""Reject new runtime monoliths and growth of the legacy ones."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LIMIT = 1200
DANGER_LIMIT = 2000
LEGACY_LIMITS = {
    "runtime/video/ddraw.c": 2610,
    "runtime/win32/imports.c": 1293,
    "runtime/video/engine.c": 1276,
}


def line_count(path: Path) -> int:
    with path.open("rb") as source:
        return sum(1 for _ in source)


def main() -> int:
    failures = []
    danger_files = []
    for path in sorted((ROOT / "runtime").rglob("*")):
        if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        relative = path.relative_to(ROOT).as_posix()
        limit = LEGACY_LIMITS.get(relative, DEFAULT_LIMIT)
        measured = line_count(path)
        if measured > limit:
            failures.append(f"{relative}: {measured} lines (limit {limit})")
        if measured >= DANGER_LIMIT:
            danger_files.append(f"{relative} ({measured} lines)")

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
    danger_summary = ", ".join(danger_files) if danger_files else "none"
    print(
        f"structure: ok (new runtime files <= {DEFAULT_LIMIT} lines; "
        f"legacy files did not grow; critical >= {DANGER_LIMIT}: {danger_summary})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
