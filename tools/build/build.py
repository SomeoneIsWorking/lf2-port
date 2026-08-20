#!/usr/bin/env python3
"""Configure and build LF2 with the required Clang toolchain."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def cached_compiler(cache: Path, language: str) -> str | None:
    key = f"CMAKE_{language}_COMPILER:FILEPATH="
    if not cache.is_file():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(key):
            return line.removeprefix(key)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=ROOT / "scratch" / "build-clang")
    parser.add_argument("--configure-only", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()

    missing = [tool for tool in ("clang", "clang++", "cmake") if shutil.which(tool) is None]
    if missing:
        raise SystemExit(
            f"missing required build tool(s): {', '.join(missing)}; "
            "on Fedora run: sudo dnf install clang cmake"
        )

    cache = build / "CMakeCache.txt"
    cached_compilers = {cached_compiler(cache, "C"), cached_compiler(cache, "CXX")} - {None}
    non_clang = {compiler for compiler in cached_compilers if "clang" not in Path(compiler).name}
    if non_clang:
        raise SystemExit(
            f"{build} uses {', '.join(sorted(non_clang))}, not Clang; choose a clean build directory"
        )

    subprocess.run(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build),
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        cwd=ROOT,
        check=True,
    )
    if not args.configure_only:
        subprocess.run(
            ["cmake", "--build", str(build), "--parallel", str(os.cpu_count() or 1)],
            cwd=ROOT,
            check=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
