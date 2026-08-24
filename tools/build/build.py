#!/usr/bin/env python3
"""Configure and build LF2.

The toolchain is whatever CC/CXX say (Clang on every machine this port is
developed on, AppleClang included); when they are unset Clang is used if it
exists and the platform default otherwise. A build directory is refused only
when it was configured with a DIFFERENT compiler than this invocation would
use -- reusing one cache across toolchains corrupts the build, which is a
hygiene rule about the cache, not a ban on any compiler.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def pick_compilers() -> tuple[str, str]:
    cc = os.environ.get("CC") or ("clang" if shutil.which("clang") else "cc")
    cxx = os.environ.get("CXX") or ("clang++" if shutil.which("clang++") else "c++")
    return cc, cxx


def cached_compiler(cache: Path, language: str) -> str | None:
    key = f"CMAKE_{language}_COMPILER:FILEPATH="
    if not cache.is_file():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(key):
            return line.removeprefix(key)
    return None


def same_program(a: str, b: str) -> bool:
    """The same executable, compared by what it resolves to -- so
    /usr/bin/clang and an absolute path to it are one compiler, while two
    different installs of clang are not."""
    try:
        return Path(shutil.which(a) or a).resolve() == Path(shutil.which(b) or b).resolve()
    except OSError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=ROOT / "scratch" / "build-clang")
    parser.add_argument("--configure-only", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()

    missing = [tool for tool in ("cmake",) if shutil.which(tool) is None]
    cc, cxx = pick_compilers()
    missing += [name for name in (cc, cxx)
                if shutil.which(name) is None and "/" not in name]
    if missing:
        raise SystemExit(
            f"missing required build tool(s): {', '.join(missing)}. Install a C "
            "toolchain and cmake (macOS: brew install cmake; Fedora: sudo dnf "
            "install clang cmake; Debian: sudo apt install build-essential cmake), "
            "or point CC/CXX at the ones you have."
        )

    cache = build / "CMakeCache.txt"
    mismatched = []
    for language, wanted in (("C", cc), ("CXX", cxx)):
        cached = cached_compiler(cache, language)
        if cached and not same_program(cached, wanted):
            mismatched.append(f"{language}={cached} (wanted {wanted})")
    if mismatched:
        raise SystemExit(
            f"{build} was configured with {'; '.join(mismatched)}. Reusing one "
            "build directory across compilers corrupts it: choose a clean "
            "build directory (--build-dir) or keep CC/CXX stable."
        )

    subprocess.run(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build),
            f"-DCMAKE_C_COMPILER={cc}",
            f"-DCMAKE_CXX_COMPILER={cxx}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        cwd=ROOT,
        check=True,
    )
    if not args.configure_only:
        subprocess.run(["cmake", "--build", str(build),
                        "--parallel", str(os.cpu_count() or 1)],
                       cwd=ROOT,
                       check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
