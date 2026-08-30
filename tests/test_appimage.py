#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "lf2_appimage", ROOT / "tools" / "build" / "appimage.py"
)
assert SPEC and SPEC.loader
APPIMAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(APPIMAGE)


def expect_refused(appdir: Path, needle: str) -> None:
    try:
        APPIMAGE.verify_no_game_content(appdir)
    except SystemExit as error:
        if needle not in str(error):
            raise AssertionError(f"expected {needle!r} in {error!r}") from error
    else:
        raise AssertionError("forbidden AppDir content was accepted")


def main() -> int:
    real_which = APPIMAGE.shutil.which
    APPIMAGE.shutil.which = lambda _name: None
    try:
        try:
            APPIMAGE.require_host_tools()
        except SystemExit as error:
            assert "sudo apt install file" in str(error)
            assert "sudo dnf install file" in str(error)
        else:
            raise AssertionError("missing file utility was accepted")
    finally:
        APPIMAGE.shutil.which = real_which

    scratch = ROOT / "scratch" / "test-appimage-tool"
    if scratch.exists():
        shutil.rmtree(scratch)
    appdir = scratch / "LF2.AppDir"
    binary = appdir / "usr" / "bin" / "lf2"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"\x7fELF-port")
    APPIMAGE.verify_no_game_content(appdir)

    original = appdir / "usr" / "share" / "LF2_v2.0a.exe"
    original.parent.mkdir(parents=True)
    original.write_bytes(b"MZ-game")
    expect_refused(appdir, "LF2_v2.0a.exe")
    original.unlink()

    extracted = appdir / "usr" / "share" / "game" / "data" / "data.txt"
    extracted.parent.mkdir(parents=True)
    extracted.write_text("game data", encoding="utf-8")
    expect_refused(appdir, "game-data directory")
    shutil.rmtree(scratch)
    print("appimage tool: positive and two negative content gates passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
