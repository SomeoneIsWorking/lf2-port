#!/usr/bin/env python3
"""Build and test the offline suite across available C compilers and optimisation levels."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from source_dependencies import DependencyError, resolve_runtime_dependencies


ROOT = Path(__file__).resolve().parents[2]


def environment(compiler: str, optimisation: str) -> dict[str, str]:
    result = os.environ.copy()
    result["CC"] = compiler
    result["CFLAGS"] = optimisation
    return result


def run_build(directory: Path, compiler: str, optimisation: str) -> None:
    result = subprocess.run(
        ["cmake", "--build", str(directory), "--parallel"],
        cwd=ROOT,
        env=environment(compiler, optimisation),
        text=True,
        capture_output=True,
        check=False,
    )
    for line in (result.stdout + result.stderr).splitlines():
        if "warning:" in line or "error:" in line:
            print(line)
    subprocess.run(
        ["cmake", "--build", str(directory), "--parallel"],
        cwd=ROOT,
        env=environment(compiler, optimisation),
        check=True,
    )


def main(arguments: list[str]) -> int:
    try:
        dependencies = resolve_runtime_dependencies(ROOT)
    except DependencyError as error:
        raise SystemExit(f"runtime dependencies: {error}") from error
    failures = 0
    ran = 0
    for compiler in ("gcc", "clang"):
        if shutil.which(compiler) is None:
            print(f"== {compiler}: not installed, skipping")
            continue
        for optimisation in ("", "-O2"):
            ran += 1
            suffix = "-O2" if optimisation else ""
            tag = f"{compiler}{optimisation}"
            directory = ROOT / "build" / f"matrix-{compiler}{suffix}"
            print(f"== {tag}: configuring in {directory.relative_to(ROOT)}")
            subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    str(directory),
                    f"-DX86PORT_DIR={dependencies['x86port']}",
                    f"-DX86PORT_JITCOMMON_DIR={dependencies['jit-common']}",
                ],
                cwd=ROOT,
                env=environment(compiler, optimisation),
                check=True,
                stdout=subprocess.DEVNULL,
            )
            print(f"== {tag}: building")
            run_build(directory, compiler, optimisation)
            print(f"== {tag}: testing")
            result = subprocess.run(
                ["ctest", "--test-dir", str(directory), "--output-on-failure", *arguments],
                cwd=ROOT,
                env={**environment(compiler, optimisation), "BUILD": str(directory)},
                check=False,
            )
            if result.returncode == 0:
                print(f"== {tag}: PASS")
            else:
                print(f"== {tag}: FAIL")
                failures = 1
    if ran < 4:
        print(f"WARNING: only {ran} configuration(s) ran -- this was not a full cross-check.")
    return failures


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
