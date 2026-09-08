#!/usr/bin/env python3
"""Build LF2's arm64 Android APK locally from the native/JIT product."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from types import ModuleType

from source_dependencies import (
    ANDROID_PORT,
    DependencyError,
    resolve_checkout,
    resolve_runtime_dependencies,
)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_WORK = ROOT / "build" / "android"
NDK_VERSION = "28.2.13676358"
PROFILE = ROOT / "platforms/android/android-port-profile.json"
_SHARED: ModuleType | None = None


def shared_android() -> ModuleType:
    global _SHARED
    if _SHARED is None:
        root = resolve_checkout(ROOT, ANDROID_PORT)
        spec = importlib.util.spec_from_file_location(
            "lf2_shared_android", root / "tools/android_port.py"
        )
        if spec is None or spec.loader is None:
            refuse(f"shared Android module cannot be loaded from {root}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        _SHARED = module
    return _SHARED


def package_profile(work: Path):
    shared = shared_android()
    profile = shared.load_android_port_profile(PROFILE)
    # --work-dir relocates title outputs only. ABI/API/prefix remain owned by the portable profile.
    return replace(
        profile, native_library=work / "native/libmain.so", jni_libs=work / "jniLibs"
    )


def refuse(message: str) -> None:
    raise SystemExit(f"android build: {message}")


def scoped_clean(path: Path) -> None:
    resolved = path.resolve()
    build = (ROOT / "build").resolve()
    if resolved == build or build not in resolved.parents:
        refuse(
            f"refusing to clean non-scoped path {resolved}; expected a child of {build}"
        )
    if resolved.exists():
        shutil.rmtree(resolved)


def require_program(name: str, package_hint: str | None = None) -> str:
    found = shutil.which(name)
    if found:
        return found
    suffix = f" Install it with: {package_hint}" if package_hint else ""
    refuse(f"missing required tool {name}.{suffix}")


def android_sdk() -> Path:
    value = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not value:
        refuse("ANDROID_HOME or ANDROID_SDK_ROOT must name the installed Android SDK")
    sdk = Path(value).expanduser().resolve()
    required = [
        sdk / "ndk" / NDK_VERSION / "build" / "cmake" / "android.toolchain.cmake",
        sdk / "platforms" / "android-35" / "android.jar",
        sdk / "build-tools" / "36.0.0" / "aapt2",
        sdk / "platform-tools" / "adb",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        refuse("required SDK components are missing:\n  " + "\n  ".join(missing))
    return sdk


def java_home() -> Path:
    return shared_android().select_java_home()


def release_signing(environment: Mapping[str, str] = os.environ) -> dict[str, str]:
    names = (
        "LF2_ANDROID_KEYSTORE",
        "LF2_ANDROID_KEY_ALIAS",
        "LF2_ANDROID_STORE_PASSWORD",
        "LF2_ANDROID_KEY_PASSWORD",
    )
    values = {name: environment.get(name, "") for name in names}
    missing = [name for name, value in values.items() if not value]
    if missing:
        refuse(
            "release signing is incomplete; set "
            + ", ".join(missing)
            + ". Refusing an unsigned APK"
        )
    keystore = Path(values["LF2_ANDROID_KEYSTORE"]).expanduser()
    if not keystore.is_file():
        refuse(f"release keystore is missing: {keystore}")
    values["LF2_ANDROID_KEYSTORE"] = str(keystore.resolve())
    return values


def android_version_name(environment: Mapping[str, str] = os.environ) -> str:
    version = environment.get("LF2_ANDROID_VERSION_NAME", "0.1.0")
    if not re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}", version):
        refuse("LF2_ANDROID_VERSION_NAME must be a three-part semantic version")
    return version


def android_version_code(version: str) -> int:
    parts = [int(part) for part in version.split(".")]
    if len(parts) != 3 or any(part > 999 for part in parts):
        refuse("Android version components must each be between 0 and 999")
    code = parts[0] * 1_000_000 + parts[1] * 1_000 + parts[2]
    if code < 1:
        refuse("Android version 0.0.0 cannot be published")
    return code


def apksigner(sdk: Path) -> Path:
    candidates = sorted((sdk / "build-tools").glob("*/apksigner"), reverse=True)
    if not candidates:
        refuse(f"Android apksigner is missing under {sdk / 'build-tools'}")
    return candidates[0]


def run(
    command: list[str], *, cwd: Path = ROOT, environment: dict[str, str] | None = None
) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def build_dependencies(work: Path, sdk: Path) -> Path:
    shared = shared_android()
    profile = package_profile(work)
    shared.build_native_dependencies(
        shared.native_dependency_request_for_profile(
            profile, sdk / "ndk" / NDK_VERSION
        ),
        2,
    )
    shared.validate_native_dependency_prefix(profile)
    return profile.prefix


def build_native(work: Path, prefix: Path, sdk: Path) -> Path:
    profile = package_profile(work)
    build = work / "native"
    toolchain = (
        sdk / "ndk" / NDK_VERSION / "build" / "cmake" / "android.toolchain.cmake"
    )
    try:
        runtime_dependencies = resolve_runtime_dependencies(ROOT)
    except DependencyError as error:
        refuse(f"runtime dependencies: {error}")
    run(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DANDROID_ABI={profile.abi}",
            f"-DANDROID_PLATFORM=android-{profile.api}",
            "-DANDROID_STL=c++_shared",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DSDL3_DIR={prefix / 'lib' / 'cmake' / 'SDL3'}",
            f"-DSDL3_image_DIR={prefix / 'lib' / 'cmake' / 'SDL3_image'}",
            f"-DSDL3_ttf_DIR={prefix / 'lib' / 'cmake' / 'SDL3_ttf'}",
            f"-DFREETYPE_LIBRARY={prefix / 'lib' / 'libfreetype.a'}",
            f"-DFREETYPE_INCLUDE_DIR_ft2build={prefix / 'include' / 'freetype2'}",
            f"-DFREETYPE_INCLUDE_DIR_freetype2={prefix / 'include' / 'freetype2'}",
            f"-DBZIP2_INCLUDE_DIR={prefix / 'include'}",
            f"-DBZIP2_LIBRARY_RELEASE={prefix / 'lib' / 'libbz2.a'}",
            f"-DLF2_FFMPEG_ROOT={prefix}",
            f"-DX86PORT_DIR={runtime_dependencies['x86port']}",
            f"-DX86PORT_JITCOMMON_DIR={runtime_dependencies['jit-common']}",
        ]
    )
    run(
        [
            "cmake",
            "--build",
            str(build),
            "--target",
            "lf2",
            "--parallel",
            str(min(os.cpu_count() or 1, 2)),
        ]
    )
    library = build / "libmain.so"
    if not library.is_file():
        refuse(f"native build did not create {library}")
    shared_android().verify_native_entry(library, sdk / "ndk" / NDK_VERSION)
    return library


def assemble_project(work: Path, prefix: Path, native: Path) -> Path:
    project = work / "project"
    scoped_clean(project)
    project.mkdir(parents=True)
    template = ROOT / "platforms/android"
    for name in ("build.gradle", "settings.gradle"):
        shutil.copy2(template / name, project / name)
    shutil.copytree(template / "app", project / "app")
    shared = shared_android()
    shared.stage_gradle_runtime(
        prefix, project, ROOT / "third_party/lucent/platforms/android/java"
    )
    wrapper_properties = project / "gradle/wrapper/gradle-wrapper.properties"
    shutil.copy2(template / "gradle-wrapper.properties", wrapper_properties)
    assets = project / "app/src/main/assets"
    shutil.copytree(ROOT / "stages", assets / "stages")
    profile = package_profile(work)
    if profile.native_library != native:
        refuse(f"native output disagrees with package profile: {native}")
    shared.stage_package_runtime(profile)
    return project


def inspect_apk(apk: Path) -> None:
    shared = shared_android()
    names = shared.inspect_apk_runtime(
        apk, shared.load_android_port_profile(PROFILE).abi
    )
    game_directories = {"game", "data", "sprite", "bg", "bgm", "music", "sound"}
    forbidden = [
        name
        for name in names
        if Path(name).name.lower() in {"lf2.exe", "lf2_v2.0a.exe", "data.txt"}
        or game_directories.intersection(part.lower() for part in Path(name).parts)
    ]
    if forbidden:
        refuse("APK contains prohibited original game paths: " + ", ".join(forbidden))


def build_apk(
    project: Path,
    sdk: Path,
    java: Path,
    signing: Mapping[str, str] | None,
    release: bool,
) -> Path:
    task = ":app:assembleRelease" if release else ":app:assembleDebug"
    environment = dict(os.environ)
    native_access = "--enable-native-access=ALL-UNNAMED"
    environment["JAVA_OPTS"] = " ".join(
        option for option in (environment.get("JAVA_OPTS", ""), native_access) if option
    )
    if signing:
        environment.update(signing)
    version = android_version_name(environment)
    environment["LF2_ANDROID_VERSION_CODE"] = str(android_version_code(version))
    profile = package_profile(project.parent)
    environment["LF2_ANDROID_JNI_LIBS"] = str(profile.jni_libs)
    environment["LF2_ANDROID_MIN_SDK"] = str(profile.api)
    environment["JAVA_HOME"] = str(java)
    run(
        [
            str(project / "gradlew"),
            "--no-daemon",
            f"-Dorg.gradle.java.home={java}",
            task,
        ],
        cwd=project,
        environment=environment,
    )
    kind = "release" if release else "debug"
    candidates = sorted(
        (project / "app" / "build" / "outputs" / "apk" / kind).glob("*.apk")
    )
    if len(candidates) != 1:
        refuse(f"expected one {kind} APK, found {len(candidates)}")
    inspect_apk(candidates[0])
    output = ROOT / "build" / "release" / f"LF2-Port-{version}-android-arm64-{kind}.apk"
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(candidates[0], output)
    if release:
        run([str(apksigner(sdk)), "verify", "--verbose", "--print-certs", str(output)])
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"android build: {output}\nandroid build: sha256 {digest}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument(
        "--release", action="store_true", help="produce a signed release APK"
    )
    parser.add_argument(
        "--native-only",
        action="store_true",
        help="build and inspect libmain.so without Gradle",
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="install and launch on exactly one adb device",
    )
    args = parser.parse_args()

    for name, hint in (
        ("git", "sudo dnf install git"),
        ("cmake", "sudo dnf install cmake"),
        ("ninja", "sudo dnf install ninja-build"),
        ("make", "sudo dnf install make"),
    ):
        require_program(name, hint)
    sdk = android_sdk()
    java = None if args.native_only else java_home()
    signing = release_signing() if args.release and not args.native_only else None
    work = args.work_dir.resolve()
    if args.clean:
        scoped_clean(work)
    work.mkdir(parents=True, exist_ok=True)

    prefix = build_dependencies(work, sdk)
    native = build_native(work, prefix, sdk)
    if args.native_only:
        print(f"android build: native library passed entrypoint inspection: {native}")
        return 0
    assert java is not None
    project = assemble_project(work, prefix, native)
    apk = build_apk(project, sdk, java, signing, args.release)
    if args.install:
        adb = sdk / "platform-tools" / "adb"
        profile = package_profile(work)
        shared = shared_android()
        for command in (
            [str(adb), "-s", profile.emulator_serial, "install", "-r", str(apk)],
            [
                str(adb),
                "-s",
                profile.emulator_serial,
                "shell",
                "am",
                "start",
                "-n",
                "io.github.someoneisworking.lf2port/.Lf2Activity",
            ],
        ):
            result = shared.with_profile_emulator_lock(profile, command)
            if result:
                refuse(f"Android device command failed with status {result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
