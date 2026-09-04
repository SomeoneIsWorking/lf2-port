#!/usr/bin/env python3
"""Empty one explicitly named LF2 scratch child without crossing its boundary."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]


class ScratchCleanError(RuntimeError):
    """The requested cleanup target is broad or escapes project scratch."""


def resolve_scratch_child(root: Path, requested: str | Path) -> Path:
    root = root.resolve()
    relative = Path(requested)
    if relative.is_absolute() or ".." in relative.parts:
        raise ScratchCleanError("absolute paths and '..' components are not allowed")
    if not relative.parts or relative == Path("scratch"):
        raise ScratchCleanError("name one child below scratch, not the whole scratch directory")
    if relative.parts[0] != "scratch":
        relative = Path("scratch") / relative
    scratch = (root / "scratch").resolve()
    target = (root / relative).resolve()
    if scratch not in target.parents:
        raise ScratchCleanError(f"{target} resolves outside {scratch}")
    return target


def empty_scratch_child(root: Path, requested: str | Path) -> tuple[Path, int]:
    target = resolve_scratch_child(root, requested)
    target.mkdir(parents=True, exist_ok=True)
    entries = sorted(target.iterdir())
    removed = 0
    for entry in entries:
        removed += sum(1 for _ in entry.rglob("*")) + 1 if entry.is_dir() else 1
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    return target, removed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", help="path below project scratch/ to empty")
    args = parser.parse_args(argv)
    try:
        target, removed = empty_scratch_child(ROOT, args.path)
    except ScratchCleanError as error:
        parser.error(str(error))
    relative = target.relative_to(ROOT)
    noun = "entry" if removed == 1 else "entries"
    print(f"scratch_clean: {relative} emptied ({removed} {noun} removed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
