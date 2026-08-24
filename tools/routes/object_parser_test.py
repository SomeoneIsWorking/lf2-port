#!/usr/bin/env python3
"""Differentially compare every native-parsed LF2 object with the original parser."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / os.environ.get("LF2_SCRATCH", "scratch") / "object_parser_test"


def configured_path(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def run_parser(binary: Path, game: Path, output: Path, original: bool) -> tuple[int, str]:
    output.mkdir(parents=True)
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_UNPACED="1",
        LF2_OBJECT_PARSER_DUMP=str(output),
        LF2_QUIT_AFTER="1",
    )
    if original:
        env["LF2_SLOW_OBJECT_PARSER"] = "1"
    else:
        env.pop("LF2_SLOW_OBJECT_PARSER", None)
    try:
        run = subprocess.run(
            [str(binary), "lf2.exe"],
            cwd=game,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=90,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return 124, "parser control exceeded 90 seconds"
    return run.returncode, run.stdout


def main() -> int:
    build = configured_path("BUILD", "scratch/build-clang")
    game = configured_path("GAME", "game")
    binary = build / "lf2"
    if not binary.is_file():
        print(f"SKIP: {binary} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    original = OUTPUT / "original"
    native = OUTPUT / "native"
    print("object parser: running LF2's original parser control...", flush=True)
    original_status, original_log = run_parser(binary, game, original, True)
    print("object parser: running the native parser...", flush=True)
    native_status, native_log = run_parser(binary, game, native, False)
    (OUTPUT / "original.log").write_text(original_log, encoding="utf-8")
    (OUTPUT / "native.log").write_text(native_log, encoding="utf-8")

    original_files = sorted(path.name for path in original.glob("*.bin"))
    native_files = sorted(path.name for path in native.glob("*.bin"))
    differing = [
        name
        for name in original_files
        if name in native_files
        and (original / name).read_bytes() != (native / name).read_bytes()
    ]
    checks = (
        (original_status == 0, f"original parser exited cleanly ({original_status})"),
        (native_status == 0, f"native parser exited cleanly ({native_status})"),
        (len(original_files) == 65, f"original parser dumped all 65 objects ({len(original_files)})"),
        (native_files == original_files, "native and original object sets are identical"),
        (not differing, f"object structures, dynamic records, sounds and checksum match ({len(differing)} differ)"),
    )
    failed = False
    for passed, description in checks:
        print(f"  {'ok  ' if passed else 'FAIL'}  {description}")
        failed |= not passed
    if failed:
        print(f"object parser test FAILED; logs and dumps: {OUTPUT}")
    else:
        print("object parser test PASSED")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
