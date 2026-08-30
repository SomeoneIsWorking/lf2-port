#!/usr/bin/env python3
"""Stage LF2's CMake install tree and build a game-file-free AppImage."""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD = ROOT / "scratch" / "build-clang"
DEFAULT_WORK = ROOT / "scratch" / "appimage"
LINUXDEPLOY_SHA256 = "421ca71d5c69ea97c6309276232990d43df1dcece0edfaa26bbf926ff96ed12e"
APPIMAGETOOL_SHA256 = "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
APPIMAGE_RUNTIME_SHA256 = "1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf"
LINUXDEPLOY_URL = (
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/"
    "linuxdeploy-x86_64.AppImage"
)
APPIMAGETOOL_URL = (
    "https://github.com/AppImage/appimagetool/releases/download/1.9.1/"
    "appimagetool-x86_64.AppImage"
)
APPIMAGE_RUNTIME_URL = (
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
)
APP_ID = "io.github.SomeoneIsWorking.lf2-port"


def refuse(message: str) -> None:
    raise SystemExit(f"appimage: {message}")


def scoped_clean(path: Path) -> None:
    resolved = path.resolve()
    scratch = (ROOT / "scratch").resolve()
    if resolved == scratch or scratch not in resolved.parents:
        refuse(f"refusing to clean non-scoped path {resolved}; expected a child of {scratch}")
    if resolved.exists():
        shutil.rmtree(resolved)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_no_game_content(appdir: Path) -> None:
    forbidden_paths = {
        "lf2_v2.0a.exe",
        "data/data.txt",
        "re/instructions.tsv",
        "gen/lf2_recomp.c",
    }
    forbidden_directories = {"game", "data", "sprite", "bg", "music", "sound"}
    failures: list[str] = []
    scanned = 0
    for path in sorted(appdir.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        scanned += 1
        relative = path.relative_to(appdir).as_posix()
        lowered = relative.lower()
        parts = {part.lower() for part in path.relative_to(appdir).parts}
        if any(lowered.endswith(candidate) for candidate in forbidden_paths):
            failures.append(f"forbidden game-derived path: {relative}")
        if parts & forbidden_directories:
            failures.append(f"forbidden game-data directory: {relative}")
        with path.open("rb") as source:
            if source.read(2) == b"MZ":
                failures.append(f"Windows executable/game payload: {relative}")
    if failures:
        refuse("AppDir contains material that may not ship:\n  " + "\n  ".join(failures))
    if scanned == 0:
        refuse(f"AppDir verification scanned 0 files in {appdir}")
    print(f"appimage: content gate scanned {scanned} files; no original game payloads found")


def verify_install_tree(appdir: Path) -> None:
    required = (
        appdir / "usr/bin/lf2",
        appdir / f"usr/share/applications/{APP_ID}.desktop",
        appdir / f"usr/share/icons/hicolor/scalable/apps/{APP_ID}.svg",
        appdir / f"usr/share/metainfo/{APP_ID}.appdata.xml",
    )
    missing = [str(path.relative_to(appdir)) for path in required if not path.is_file()]
    if missing:
        refuse("CMake install tree is missing: " + ", ".join(missing))
    if not (appdir / "usr/bin/stages").is_dir():
        refuse("CMake install tree is missing the port-owned stages/ resource directory")
    verify_no_game_content(appdir)


def stage(build_dir: Path, appdir: Path) -> None:
    if not (build_dir / "lf2").is_file():
        refuse(f"{build_dir}/lf2 is missing; build the release target first")
    scoped_clean(appdir)
    appdir.mkdir(parents=True)
    environment = dict(os.environ)
    environment["DESTDIR"] = str(appdir)
    subprocess.run(
        ["cmake", "--install", str(build_dir), "--prefix", "/usr"],
        cwd=ROOT,
        env=environment,
        check=True,
    )
    verify_install_tree(appdir)


def verified_tool(path: Path, expected: str, name: str) -> Path:
    if not path.is_file():
        refuse(f"{name} is missing: {path}")
    measured = sha256(path)
    if measured != expected:
        refuse(
            f"{name} SHA256 is {measured}, expected {expected}; "
            "the release toolchain is pinned and will not run unverified code"
        )
    path.chmod(path.stat().st_mode | 0o111)
    return path.resolve()


def fetch_tool(path: Path, url: str, expected: str, name: str) -> None:
    if path.is_file() and sha256(path) == expected:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(path.suffix + ".download")
    partial.unlink(missing_ok=True)
    print(f"appimage: downloading pinned {name} -> {path}")
    try:
        urllib.request.urlretrieve(url, partial)
    except OSError as error:
        partial.unlink(missing_ok=True)
        refuse(f"could not download {name} from {url}: {error}")
    measured = sha256(partial)
    if measured != expected:
        partial.unlink(missing_ok=True)
        refuse(f"downloaded {name} SHA256 is {measured}, expected {expected}")
    partial.replace(path)


def verify_artifact(appimage: Path, extraction_root: Path) -> None:
    scoped_clean(extraction_root)
    extraction_root.mkdir(parents=True)
    environment = dict(os.environ)
    environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    subprocess.run(
        [str(appimage.resolve()), "--appimage-extract"],
        cwd=extraction_root,
        env=environment,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    extracted = extraction_root / "squashfs-root"
    if not extracted.is_dir():
        refuse(f"{appimage} did not extract a squashfs-root for final artifact verification")
    verify_no_game_content(extracted)


def build_appimage(appdir: Path, linuxdeploy: Path, appimagetool: Path,
                   runtime: Path, output: Path, version: str, extraction_root: Path) -> None:
    if platform.system() != "Linux":
        refuse("AppImage packaging is supported only on Linux")
    if platform.machine() not in {"x86_64", "AMD64"}:
        refuse(f"the release workflow currently supports x86_64, not {platform.machine()}")
    linuxdeploy = verified_tool(linuxdeploy, LINUXDEPLOY_SHA256, "linuxdeploy")
    appimagetool = verified_tool(appimagetool, APPIMAGETOOL_SHA256, "appimagetool")
    runtime = verified_tool(runtime, APPIMAGE_RUNTIME_SHA256, "AppImage runtime")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    environment = dict(os.environ)
    environment.update(
        {
            "APPIMAGE_EXTRACT_AND_RUN": "1",
            "ARCH": "x86_64",
            "VERSION": version,
        }
    )
    subprocess.run(
        [
            str(linuxdeploy),
            "--appdir",
            str(appdir.resolve()),
            "--desktop-file",
            str(appdir / f"usr/share/applications/{APP_ID}.desktop"),
            "--icon-file",
            str(appdir / f"usr/share/icons/hicolor/scalable/apps/{APP_ID}.svg"),
        ],
        cwd=output.parent,
        env=environment,
        check=True,
    )
    verify_no_game_content(appdir)
    subprocess.run(
        [str(appimagetool), "--runtime-file", str(runtime),
         str(appdir.resolve()), str(output.resolve())],
        cwd=output.parent,
        env=environment,
        check=True,
    )
    if not output.is_file():
        refuse(f"appimagetool reported success but did not create {output}")
    verify_artifact(output, extraction_root)
    print(f"appimage: wrote {output} ({output.stat().st_size} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--linuxdeploy", type=Path,
                        default=ROOT / "scratch/tools/linuxdeploy-x86_64.AppImage")
    parser.add_argument("--appimagetool", type=Path,
                        default=ROOT / "scratch/tools/appimagetool-x86_64.AppImage")
    parser.add_argument("--runtime", type=Path,
                        default=ROOT / "scratch/tools/runtime-x86_64")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "scratch/release/LF2-Port-x86_64.AppImage")
    parser.add_argument("--version", default="dev")
    parser.add_argument("--fetch-tools", action="store_true")
    parser.add_argument("--stage-only", action="store_true")
    args = parser.parse_args()

    appdir = args.work_dir.resolve() / "LF2.AppDir"
    stage(args.build_dir.resolve(), appdir)
    if not args.stage_only:
        if args.fetch_tools:
            fetch_tool(args.linuxdeploy.resolve(), LINUXDEPLOY_URL, LINUXDEPLOY_SHA256, "linuxdeploy")
            fetch_tool(args.appimagetool.resolve(), APPIMAGETOOL_URL, APPIMAGETOOL_SHA256, "appimagetool")
            fetch_tool(args.runtime.resolve(), APPIMAGE_RUNTIME_URL, APPIMAGE_RUNTIME_SHA256, "AppImage runtime")
        build_appimage(appdir, args.linuxdeploy.resolve(), args.appimagetool.resolve(),
                       args.runtime.resolve(), args.output.resolve(), args.version,
                       args.work_dir.resolve() / "extracted-artifact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
