#!/usr/bin/env python3
"""Run the Clang format ratchet and clang-tidy against real compile commands."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
ADOPTED_C_SOURCES = (
    ROOT / "runtime" / "log" / "lf2_log.h",
    ROOT / "runtime" / "app" / "environment.c",
    ROOT / "runtime" / "app" / "environment.h",
    ROOT / "runtime" / "cpu" / "jit_executor.c",
    ROOT / "runtime" / "cpu" / "jit_executor.h",
    ROOT / "runtime" / "overrides" / "native_override.c",
    ROOT / "runtime" / "overrides" / "native_override.h",
    ROOT / "runtime" / "ui" / "rmlui_system.h",
    ROOT / "tests" / "test_lf2_log.cpp",
    ROOT / "runtime" / "app" / "pause.c",
    ROOT / "runtime" / "app" / "update.c",
    ROOT / "runtime" / "app" / "update.h",
    ROOT / "runtime" / "app" / "function_keys.c",
    ROOT / "runtime" / "app" / "function_keys.h",
    ROOT / "runtime" / "audio" / "dsound.c",
    ROOT / "runtime" / "audio" / "dsound.h",
    ROOT / "runtime" / "video" / "hostwin.h",
    ROOT / "runtime" / "platform" / "window_policy.c",
    ROOT / "runtime" / "platform" / "window_policy.h",
    ROOT / "runtime" / "win32" / "win32.c",
    ROOT / "runtime" / "win32" / "paths.c",
    ROOT / "runtime" / "win32" / "paths.h",
    ROOT / "runtime" / "input" / "keyboard.c",
    ROOT / "runtime" / "input" / "keyboard.h",
    ROOT / "runtime" / "input" / "touch_input.h",
    ROOT / "runtime" / "input" / "touch_pointer_state.h",
    ROOT / "runtime" / "overrides" / "input.c",
    ROOT / "runtime" / "overrides" / "boot_guest.c",
    ROOT / "runtime" / "overrides" / "cheats.c",
    ROOT / "runtime" / "overrides" / "cheats.h",
    ROOT / "runtime" / "overrides" / "object_parser.c",
    ROOT / "runtime" / "overrides" / "object_parser.h",
    ROOT / "runtime" / "overrides" / "object_frames.c",
    ROOT / "runtime" / "overrides" / "startup_frontend.c",
    ROOT / "runtime" / "overrides" / "startup_init.c",
    ROOT / "runtime" / "overrides" / "startup_init.h",
    ROOT / "runtime" / "overrides" / "startup_world.c",
    ROOT / "runtime" / "video" / "backdrop.h",
    ROOT / "runtime" / "video" / "render.c",
    ROOT / "runtime" / "video" / "raster.h",
    ROOT / "runtime" / "video" / "texrect.h",
    ROOT / "runtime" / "ui" / "device_icons.c",
    ROOT / "runtime" / "ui" / "ui_rgba.c",
    ROOT / "runtime" / "ui" / "ui_rgba.h",
    ROOT / "tests" / "test_backdrop.c",
    ROOT / "tests" / "test_keyboard.c",
    ROOT / "tests" / "test_function_keys.c",
    ROOT / "tests" / "test_cheats.c",
    ROOT / "tests" / "test_ui_rgba.c",
    ROOT / "tests" / "test_texrect.c",
)

ADOPTED_C_TIDY_SOURCES = (
    ROOT / "runtime" / "app" / "environment.c",
    ROOT / "runtime" / "cpu" / "jit_executor.c",
    ROOT / "runtime" / "overrides" / "native_override.c",
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
    for source in [*ADOPTED_C_TIDY_SOURCES, *cpp_sources()]:
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
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "clang")
    args = parser.parse_args()
    return check_format() if args.mode == "format" else check_tidy(args.build_dir.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
