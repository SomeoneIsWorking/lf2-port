#!/usr/bin/env python3
"""Build and package LF2's asset-free browser target."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))
from source_dependencies import (
    resolve_runtime_dependencies,
    resolve_web_dependency,
)

sys.path.insert(0, str(ROOT))
import bootstrap


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emsdk", type=Path, default=os.environ.get("EMSDK"))
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--configure-only", action="store_true")
    args = parser.parse_args()
    if args.emsdk is None:
        parser.error("Emscripten is missing; set EMSDK or pass --emsdk")
    emsdk = Path(args.emsdk).resolve()
    emcmake = emsdk / "upstream/emscripten/emcmake"
    if not emcmake.is_file():
        parser.error(f"Emscripten is missing emcmake: {emcmake}")

    bootstrap.ensure_submodules()
    assets = bootstrap.ensure_port_assets()
    runtime = resolve_runtime_dependencies(ROOT)
    web_port = resolve_web_dependency(ROOT)
    environment = dict(os.environ, PORT_ASSETS_DIR=str(assets), TMPDIR=str(ROOT / "scratch" / "web"))
    Path(environment["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    subprocess.run([
        sys.executable, str(web_port / "tools/web_port.py"),
        "--emsdk", str(emsdk), "--jobs", str(args.jobs),
    ], cwd=ROOT, env=environment, check=True)
    prefix = web_port / "build/prefix"
    build = ROOT / "build/web"
    configure = [
        str(emcmake), "cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_C_FLAGS=-pthread", "-DCMAKE_CXX_FLAGS=-pthread",
        "-DLUCENT_BUILD_WEB=ON", f"-DLF2_FFMPEG_ROOT={prefix}",
        f"-DX86PORT_DIR={runtime['x86port']}",
        f"-DX86PORT_JITCOMMON_DIR={runtime['jit-common']}",
        f"-DCMAKE_PREFIX_PATH={prefix}", f"-DCMAKE_FIND_ROOT_PATH={prefix}",
        f"-DZLIB_INCLUDE_DIR={prefix / 'include'}",
        f"-DZLIB_LIBRARY={prefix / 'lib/libzlibstatic.a'}",
        "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON", "-DZYAN_NO_LIBC=ON",
        f"-DCMAKE_TOOLCHAIN_FILE={emsdk / 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
    ]
    subprocess.run(configure, cwd=ROOT, env=environment, check=True)
    if args.configure_only:
        return 0
    subprocess.run(["cmake", "--build", str(build), "--target", "lf2", "-j", str(args.jobs)],
                   cwd=ROOT, env=environment, check=True)
    package = [sys.executable, str(web_port / "tools/package.py"),
               "--destination", str(ROOT / "build/release/web"),
               "--lucent", str(ROOT / "third_party/lucent")]
    for name in ("lf2.js", "lf2.wasm"):
        package.extend(["--file", f"{name}={build / name}"])
    for name in ("index.html", "app.mjs", "style.css", "manifest.webmanifest"):
        package.extend(["--file", f"{name}={ROOT / 'web' / name}"])
    subprocess.run(package, cwd=ROOT, env=environment, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
