#!/usr/bin/env python3
"""Build the pinned SDL release stack into a project-local prefix.

Linux releases use an older distribution for glibc compatibility, where SDL3 packages are not
available. This tool owns the exact upstream revisions and CMake feature set for the manually
invoked local release build.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_WORK = ROOT / "scratch" / "release-deps"


@dataclass(frozen=True)
class Dependency:
    name: str
    repository: str
    revision: str
    cmake_options: tuple[str, ...]


DEPENDENCIES = (
    Dependency(
        "SDL",
        "https://github.com/libsdl-org/SDL.git",
        "147a8ee32dbf9ac02f3794964490687b6bbda1bc",
        (
            "-DSDL_SHARED=ON",
            "-DSDL_STATIC=OFF",
            "-DSDL_TEST_LIBRARY=OFF",
            "-DSDL_TESTS=OFF",
            "-DSDL_EXAMPLES=OFF",
        ),
    ),
    Dependency(
        "SDL_image",
        "https://github.com/libsdl-org/SDL_image.git",
        "bec9134a26c7d0f31b36d6083c25296e04cabff5",
        (
            "-DBUILD_SHARED_LIBS=ON",
            "-DSDLIMAGE_INSTALL=ON",
            "-DSDLIMAGE_STRICT=ON",
            "-DSDLIMAGE_VENDORED=OFF",
            "-DSDLIMAGE_SAMPLES=OFF",
            "-DSDLIMAGE_TESTS=OFF",
            "-DSDLIMAGE_ANI=OFF",
            "-DSDLIMAGE_AVIF=OFF",
            "-DSDLIMAGE_BMP=OFF",
            "-DSDLIMAGE_GIF=OFF",
            "-DSDLIMAGE_JPG=OFF",
            "-DSDLIMAGE_LBM=OFF",
            "-DSDLIMAGE_PCX=OFF",
            "-DSDLIMAGE_PNG=OFF",
            "-DSDLIMAGE_PNM=OFF",
            "-DSDLIMAGE_QOI=OFF",
            "-DSDLIMAGE_SVG=ON",
            "-DSDLIMAGE_TGA=OFF",
            "-DSDLIMAGE_TIF=OFF",
            "-DSDLIMAGE_WEBP=OFF",
            "-DSDLIMAGE_XCF=OFF",
            "-DSDLIMAGE_XPM=OFF",
            "-DSDLIMAGE_XV=OFF",
        ),
    ),
    Dependency(
        "SDL_ttf",
        "https://github.com/libsdl-org/SDL_ttf.git",
        "a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b",
        (
            "-DBUILD_SHARED_LIBS=ON",
            "-DSDLTTF_INSTALL=ON",
            "-DSDLTTF_STRICT=ON",
            "-DSDLTTF_VENDORED=OFF",
            "-DSDLTTF_SAMPLES=OFF",
            "-DSDLTTF_HARFBUZZ=ON",
            "-DSDLTTF_PLUTOSVG=OFF",
        ),
    ),
)


def refuse(message: str) -> None:
    raise SystemExit(f"release dependencies: {message}")


def scoped_clean(path: Path) -> None:
    resolved = path.resolve()
    scratch = (ROOT / "scratch").resolve()
    if resolved == scratch or scratch not in resolved.parents:
        refuse(f"refusing to clean non-scoped path {resolved}; expected a child of {scratch}")
    if resolved.exists():
        shutil.rmtree(resolved)


def require_tools() -> None:
    missing = [name for name in ("cmake", "git", "ninja", "clang", "clang++") if not shutil.which(name)]
    if missing:
        refuse("missing required tool(s): " + ", ".join(missing))


def checkout(dependency: Dependency, sources: Path) -> Path:
    target = sources / dependency.name
    target.mkdir()
    subprocess.run(["git", "init", "--quiet"], cwd=target, check=True)
    subprocess.run(["git", "remote", "add", "origin", dependency.repository], cwd=target, check=True)
    subprocess.run(["git", "fetch", "--depth", "1", "origin", dependency.revision], cwd=target, check=True)
    subprocess.run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=target, check=True)
    measured = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=target, check=True, text=True, capture_output=True
    ).stdout.strip()
    if measured != dependency.revision:
        refuse(f"{dependency.name} checkout is {measured}, expected {dependency.revision}")
    return target


def build(dependency: Dependency, source: Path, builds: Path, prefix: Path, environment: dict[str, str]) -> None:
    build_directory = builds / dependency.name
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build_directory),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={prefix}",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            *dependency.cmake_options,
        ],
        cwd=ROOT,
        env=environment,
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(build_directory), "--parallel", str(os.cpu_count() or 1)],
        cwd=ROOT,
        env=environment,
        check=True,
    )
    subprocess.run(["cmake", "--install", str(build_directory)], cwd=ROOT, env=environment, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        refuse("the AppImage dependency stack currently supports Linux x86_64 only")
    require_tools()

    work = args.work_dir.resolve()
    scoped_clean(work)
    sources = work / "sources"
    builds = work / "builds"
    prefix = work / "prefix"
    sources.mkdir(parents=True)
    builds.mkdir()
    prefix.mkdir()

    environment = dict(os.environ)
    environment.update(
        {
            "CC": "clang",
            "CXX": "clang++",
            "CMAKE_PREFIX_PATH": str(prefix),
            "PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"),
        }
    )
    for dependency in DEPENDENCIES:
        source = checkout(dependency, sources)
        build(dependency, source, builds, prefix, environment)
    print(f"release dependencies: installed pinned SDL stack in {prefix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
