#!/usr/bin/env python3
"""Stage LF2's verified native binary as a self-contained macOS app bundle."""

from __future__ import annotations

import argparse
import os
import plistlib
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYSTEM_PREFIXES = ("/usr/lib/", "/System/", "/Library/Apple/")
BUNDLE_ID = "io.github.SomeoneIsWorking.lf2-port"
BUNDLE_NAME = "LF2 Port"


def refuse(message: str) -> None:
    raise SystemExit(f"macos: {message}")


def run(command: list[str]) -> str:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no output"
        refuse(f"command failed ({result.returncode}): {' '.join(command)}\n  {detail}")
    return result.stdout


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        refuse(f"{label} is missing: {path}")
    return path


def linked_names(binary: Path) -> list[str]:
    lines = run(["otool", "-L", str(binary)]).splitlines()[1:]
    return [line.strip().split(" (", 1)[0] for line in lines if line.strip()]


def rpaths(binary: Path) -> list[str]:
    lines = run(["otool", "-l", str(binary)]).splitlines()
    result: list[str] = []
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        for following in lines[index : index + 5]:
            value = following.strip()
            if value.startswith("path "):
                result.append(value[5:].split(" (offset", 1)[0].strip())
                break
    return result


def bundled(name: str) -> bool:
    return bool(name) and not name.startswith(SYSTEM_PREFIXES)


def resolve(name: str, owner: Path) -> Path | None:
    if name.startswith("@loader_path/"):
        return (owner.parent / name.removeprefix("@loader_path/")).resolve()
    if name.startswith("@executable_path/"):
        return None
    if name.startswith("@rpath/"):
        leaf = name.removeprefix("@rpath/")
        for base in rpaths(owner):
            candidate = (owner.parent / leaf) if base.startswith("@") else Path(base) / leaf
            if candidate.is_file():
                return candidate.resolve()
        return None
    return Path(name)


def copy_closure(binary: Path, frameworks: Path) -> dict[str, str]:
    staged: dict[str, str] = {}
    pending = [(name, binary) for name in linked_names(binary) if bundled(name)]
    seen: set[tuple[Path, str]] = set()
    while pending:
        name, owner = pending.pop()
        key = (owner, name)
        if key in seen:
            continue
        seen.add(key)
        source = resolve(name, owner)
        if source is None or not source.is_file():
            refuse(f"{owner.name} needs {name}, which is not available")
        destination = frameworks / source.name
        if not destination.exists():
            shutil.copy2(source, destination)
            destination.chmod(0o755)
        if not name.startswith("@"):
            staged[name] = destination.name
        pending.extend((further, source) for further in linked_names(source) if bundled(further))
    return staged


def rewrite(target: Path, replacements: dict[str, str], *, executable: bool) -> None:
    for original, name in replacements.items():
        run(["install_name_tool", "-change", original, f"@rpath/{name}", str(target)])
    for path in rpaths(target):
        if not path.startswith("@"):
            run(["install_name_tool", "-delete_rpath", path, str(target)])
    if executable:
        run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(target)])
    else:
        run(["install_name_tool", "-id", f"@rpath/{target.name}", str(target)])
        run(["install_name_tool", "-add_rpath", "@loader_path", str(target)])


def info_plist() -> bytes:
    return plistlib.dumps(
        {
            "CFBundleName": BUNDLE_NAME,
            "CFBundleDisplayName": BUNDLE_NAME,
            "CFBundleIdentifier": BUNDLE_ID,
            "CFBundleExecutable": "lf2",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": os.environ.get("LF2_VERSION", "0.1.0"),
            "CFBundleVersion": os.environ.get("LF2_VERSION", "0.1.0"),
            "LSMinimumSystemVersion": "13.0",
            "NSHighResolutionCapable": True,
            "NSDesktopFolderUsageDescription": "LF2 needs to read the game installation you choose.",
            "NSDocumentsFolderUsageDescription": "LF2 needs to read the game installation you choose.",
            "NSDownloadsFolderUsageDescription": "LF2 needs to read the game installation you choose.",
            "NSRemovableVolumesUsageDescription": "LF2 needs to read the game installation you choose.",
        }
    )


def verify_no_game_files(bundle: Path) -> None:
    forbidden = {"lf2.exe", "lf2_v2.0a.exe", "data.txt"}
    scanned = 0
    failures: list[str] = []
    for path in bundle.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        scanned += 1
        with path.open("rb") as source:
            is_windows_executable = source.read(2) == b"MZ"
        if path.name.lower() in forbidden or is_windows_executable:
            failures.append(str(path.relative_to(bundle)))
    if not scanned:
        refuse(f"bundle content check scanned zero files: {bundle}")
    if failures:
        refuse("bundle contains original game material: " + ", ".join(failures))


def stage(binary: Path, output: Path) -> None:
    platform_system = os.uname().sysname
    if platform_system != "Darwin":
        refuse(f"macOS packaging requires Darwin, found {platform_system}")
    binary = require_file(binary.resolve(), "native release binary")
    if output.exists():
        shutil.rmtree(output)
    contents = output / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    frameworks = contents / "Frameworks"
    for directory in (macos, resources, frameworks):
        directory.mkdir(parents=True, exist_ok=True)
    executable = macos / "lf2"
    shutil.copy2(binary, executable)
    executable.chmod(0o755)
    (contents / "Info.plist").write_bytes(info_plist())
    shutil.copytree(ROOT / "stages", macos / "stages")
    staged = copy_closure(executable, frameworks)
    rewrite(executable, staged, executable=True)
    for library in sorted(frameworks.glob("*.dylib")):
        rewrite(library, staged, executable=False)
    run(["codesign", "--force", "--deep", "--sign", "-", str(output)])
    run(["codesign", "--verify", "--deep", str(output)])
    verify_no_game_files(output)
    result = subprocess.run(
        [str(executable), "--no-window", "--selftest"],
        env={key: value for key, value in os.environ.items() if key != "GAME_PC_DIR"},
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode not in (0, 77):
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        refuse(f"bundled selftest failed with {result.returncode}: {detail}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/macos/lf2")
    parser.add_argument("--output", type=Path, default=ROOT / "build/release/LF2-Port.app")
    args = parser.parse_args()
    stage(args.binary, args.output)
    print(f"macos: created {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
