#!/usr/bin/env python3
"""Exercise direct, directory, nested-ZIP, duplicate, and traversal setup selections."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


def run(
    binary: Path, selection: Path, config: Path, expected_success: bool
) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ)
    environment["XDG_CONFIG_HOME"] = str(config)
    result = subprocess.run(
        [str(binary), str(selection)],
        text=True,
        capture_output=True,
        env=environment,
        check=False,
    )
    if (result.returncode == 0) != expected_success:
        raise AssertionError(
            f"selection {selection} returned {result.returncode}\nstdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_game_selection.py <binary> <scratch-root>")
    binary = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve() / "game-selection"
    if root.exists():
        shutil.rmtree(root)
    source = root / "source"
    config = root / "config"
    source.mkdir(parents=True)

    executable = source / "lf2.exe"
    executable.write_bytes(b"fixture")
    data = source / "data"
    data.mkdir()
    (data / "data.txt").write_text("file: data/fixture.dat\n", encoding="ascii")
    (data / "fixture.dat").write_bytes(b"asset")
    direct = run(binary, executable, config, True)
    assert Path(direct.stdout.strip()) == executable
    directory = run(binary, source, config, True)
    assert Path(directory.stdout.strip()) == executable

    nested = root / "nested.zip"
    with zipfile.ZipFile(nested, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Install/LF2/lf2.exe", b"fixture")
        archive.writestr("Install/LF2/data/data.txt", b"file: data/fixture.dat\n")
        archive.writestr("Install/LF2/data/fixture.dat", b"asset")
    nested_result = run(binary, nested, config, True)
    assert Path(nested_result.stdout.strip()).name.lower() == "lf2.exe"
    assert Path(nested_result.stdout.strip()).is_file()
    repeated_result = run(binary, nested, config, True)
    assert Path(repeated_result.stdout.strip()) == Path(nested_result.stdout.strip())
    assert not list(config.rglob("*.preparing"))
    assert not list(config.rglob("*.previous"))

    accepted = Path(nested_result.stdout.strip())
    with zipfile.ZipFile(nested, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Broken/LF2/lf2.exe", b"fixture")
        archive.writestr("Broken/LF2/data/data.txt", b"file: data/missing.dat\n")
    assert "missing game data" in run(binary, nested, config, False).stderr
    assert accepted.read_bytes() == b"fixture"
    assert not list(config.rglob("*.preparing"))
    assert not list(config.rglob("*.previous"))

    duplicate = root / "duplicate.zip"
    with zipfile.ZipFile(duplicate, "w") as archive:
        archive.writestr("one/lf2.exe", b"one")
        archive.writestr("two/LF2.EXE", b"two")
    assert "more than one" in run(binary, duplicate, config, False).stderr

    traversal = root / "traversal.zip"
    with zipfile.ZipFile(traversal, "w") as archive:
        archive.writestr("../lf2.exe", b"fixture")
    assert "unsafe path" in run(binary, traversal, config, False).stderr

    oversized = root / "oversized.zip"
    with oversized.open("wb") as archive:
        archive.seek(512 * 1024 * 1024)
        archive.write(b"\0")
    assert "compressed byte limit" in run(binary, oversized, config, False).stderr

    print(
        "game selection: complete direct/ZIP installs accepted; invalid replacement preserved; duplicate, "
        "traversal, and budget checks passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
