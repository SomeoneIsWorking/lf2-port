#!/usr/bin/env python3
"""Collect an exact, asset-free release package set with SHA-256 checksums."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


def expected_names(version: str) -> set[str]:
    return {
        "LF2-Port-x86_64.AppImage",
        "LF2-Port-x86_64.AppImage.zsync",
        "LF2-Port-macos-arm64.zip",
        f"LF2-Port-{version}-android-arm64-release.apk",
    }


def collect(dist: Path, destination: Path, version: str) -> None:
    expected = expected_names(version)
    found = [path for path in dist.rglob("*") if path.is_file()]
    names = [path.name for path in found]
    if len(names) != len(set(names)):
        raise ValueError("downloaded artifacts contain duplicate filenames")
    if set(names) != expected:
        raise ValueError(
            f"release assets differ from expected package set: "
            f"missing={sorted(expected - set(names))}, unexpected={sorted(set(names) - expected)}"
        )
    if any(path.is_symlink() or path.stat().st_size == 0 for path in found):
        raise ValueError("release assets contain a symlink or empty package")
    destination.mkdir(parents=True, exist_ok=True)
    checksums: list[str] = []
    for source in sorted(found, key=lambda path: path.name):
        target = destination / source.name
        shutil.copyfile(source, target)
        with target.open("rb") as content:
            digest = hashlib.file_digest(content, "sha256").hexdigest()
        checksums.append(f"{digest}  {target.name}")
    (destination / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    arguments = parser.parse_args()
    try:
        collect(arguments.dist, arguments.output, arguments.version)
    except ValueError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
