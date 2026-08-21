#!/usr/bin/env python3
"""Run the Clang format ratchet and clang-tidy against real compile commands."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
ADOPTED_C_SOURCES = (
    ROOT / "runtime" / "app" / "pause.c",
    ROOT / "runtime" / "video" / "hostwin.h",
    ROOT / "runtime" / "win32" / "win32.c",
    ROOT / "runtime" / "input" / "keyboard.c",
    ROOT / "runtime" / "input" / "keyboard.h",
    ROOT / "runtime" / "overrides" / "input.c",
    ROOT / "runtime" / "video" / "backdrop.h",
    ROOT / "runtime" / "video" / "render.c",
    ROOT / "runtime" / "video" / "texrect.h",
    ROOT / "tests" / "test_backdrop.c",
    ROOT / "tests" / "test_keyboard.c",
    ROOT / "tests" / "test_texrect.c",
)


def require(tool: str) -> str:
    path = shutil.which(tool)
    if path is None:
        raise SystemExit(
            f"{tool} is required; on Fedora run: sudo dnf install clang-tools-extra"
        )
    return path


def cpp_sources() -> list[Path]:
    return sorted((ROOT / "runtime").rglob("*.cpp"))


def check_format() -> int:
    clang_format = require("clang-format")
    sources = [*ADOPTED_C_SOURCES, *cpp_sources()]
    result = subprocess.run(
        [clang_format, "--dry-run", "--Werror", *map(str, sources)], cwd=ROOT, check=False
    )
    return result.returncode


def check_tidy(build: Path) -> int:
    clang_tidy = require("clang-tidy")
    compile_commands = build / "compile_commands.json"
    if not compile_commands.is_file():
        raise SystemExit(f"missing {compile_commands}; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    failed = False
    for source in cpp_sources():
        result = subprocess.run(
            [
                clang_tidy,
                "-p",
                str(build),
                "--quiet",
                '--line-filter=[{"name":".*/runtime/.*"}]',
                str(source),
            ],
            cwd=ROOT,
            check=False,
        )
        failed |= result.returncode != 0
    return int(failed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("format", "tidy"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "scratch" / "build-clang")
    args = parser.parse_args()
    return check_format() if args.mode == "format" else check_tidy(args.build_dir.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
