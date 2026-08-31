#!/usr/bin/env python3
"""Build LF2's arm64 Android APK locally, without committing generated output."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import zipfile
from collections.abc import Mapping
from pathlib import Path

from release_dependencies import DEPENDENCIES

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_WORK = ROOT / "scratch" / "android"
ANDROID_ABI = "arm64-v8a"
ANDROID_API = 24
NDK_VERSION = "28.2.13676358"
FFMPEG_REPOSITORY = "https://github.com/FFmpeg/FFmpeg.git"
FFMPEG_REVISION = "db69d06eeeab4f46da15030a80d539efb4503ca8"  # n7.1.1
BZIP2_REPOSITORY = "https://sourceware.org/git/bzip2.git"
BZIP2_REVISION = "6a8690fc8d26c815e798c588f796eabe9d684cf0"  # bzip2-1.0.8


def refuse(message: str) -> None:
    raise SystemExit(f"android build: {message}")


def scoped_clean(path: Path) -> None:
    resolved = path.resolve()
    scratch = (ROOT / "scratch").resolve()
    if resolved == scratch or scratch not in resolved.parents:
        refuse(f"refusing to clean non-scoped path {resolved}; expected a child of {scratch}")
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


def supported_java_major(major: int) -> bool:
    return 17 <= major <= 26


def java_home() -> Path:
    candidates: list[Path] = []
    configured = os.environ.get("JAVA_HOME")
    if configured:
        candidates.append(Path(configured))
    java = shutil.which("java")
    if java:
        candidates.append(Path(java).resolve().parent.parent)
    candidates.extend(Path("/usr/lib/jvm").glob("*/bin/java"))
    seen: set[Path] = set()
    for candidate in candidates:
        home = candidate if candidate.name != "java" else candidate.parent.parent
        home = home.resolve()
        if home in seen or not (home / "bin/java").is_file() or not (home / "bin/javac").is_file():
            continue
        seen.add(home)
        result = subprocess.run(
            [str(home / "bin/java"), "-version"], text=True, capture_output=True, check=False
        )
        first = (result.stderr + result.stdout).splitlines()
        try:
            major = int(first[0].split('version "', 1)[1].split(".", 1)[0])
        except (IndexError, ValueError):
            continue
        if supported_java_major(major):
            return home
    refuse(
        "Android Gradle needs JDK 17 through 26. On this DNF system install it with: "
        "sudo dnf install java-17-openjdk-devel; then set JAVA_HOME to that JDK"
    )


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
        refuse("release signing is incomplete; set " + ", ".join(missing) + ". Refusing an unsigned APK")
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


def run(command: list[str], *, cwd: Path = ROOT, environment: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def checkout(repository: str, revision: str, destination: Path) -> Path:
    if (destination / ".git").exists():
        measured = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=destination, text=True, capture_output=True, check=True
        ).stdout.strip()
        if measured == revision:
            return destination
        scoped_clean(destination)
    destination.mkdir(parents=True)
    run(["git", "init", "--quiet"], cwd=destination)
    run(["git", "remote", "add", "origin", repository], cwd=destination)
    run(["git", "fetch", "--depth", "1", "origin", revision], cwd=destination)
    run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=destination)
    measured = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=destination, text=True, capture_output=True, check=True
    ).stdout.strip()
    if measured != revision:
        refuse(f"checkout {destination.name} is {measured}, expected {revision}")
    return destination


def prepare_sources(work: Path) -> dict[str, Path]:
    sources = work / "sources"
    sources.mkdir(parents=True, exist_ok=True)
    result: dict[str, Path] = {}
    for dependency in DEPENDENCIES:
        result[dependency.name] = checkout(dependency.repository, dependency.revision, sources / dependency.name)
    ttf = result["SDL_ttf"]
    run(["git", "submodule", "update", "--init", "--depth", "1", "external/freetype"], cwd=ttf)
    result["ffmpeg"] = checkout(FFMPEG_REPOSITORY, FFMPEG_REVISION, sources / "ffmpeg")
    result["bzip2"] = checkout(BZIP2_REPOSITORY, BZIP2_REVISION, sources / "bzip2")
    return result


def cmake_build(source: Path, build: Path, prefix: Path, sdk: Path, options: list[str]) -> None:
    toolchain = sdk / "ndk" / NDK_VERSION / "build" / "cmake" / "android.toolchain.cmake"
    command = [
        "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DANDROID_ABI={ANDROID_ABI}",
        f"-DANDROID_PLATFORM=android-{ANDROID_API}", "-DANDROID_STL=c++_shared",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DCMAKE_PREFIX_PATH={prefix}",
        "-DCMAKE_INSTALL_LIBDIR=lib", *options,
    ]
    run(command)
    run(["cmake", "--build", str(build), "--parallel", str(os.cpu_count() or 1)])
    run(["cmake", "--install", str(build)])


def dependency_fingerprint() -> dict[str, object]:
    return {
        "abi": ANDROID_ABI,
        "api": ANDROID_API,
        "ndk": NDK_VERSION,
        "dependencies": {dependency.name: dependency.revision for dependency in DEPENDENCIES},
        "ffmpeg": FFMPEG_REVISION,
        "bzip2": BZIP2_REVISION,
        "ffmpeg_pic": True,
    }


def build_dependencies(work: Path, sources: dict[str, Path], sdk: Path) -> Path:
    prefix = work / "prefix"
    marker = work / "dependencies.json"
    fingerprint = dependency_fingerprint()
    if marker.is_file() and prefix.is_dir() and json.loads(marker.read_text()) == fingerprint:
        print(f"android build: reusing pinned native dependencies in {prefix}")
        return prefix

    scoped_clean(work / "builds")
    scoped_clean(prefix)
    builds = work / "builds"
    builds.mkdir(parents=True)
    prefix.mkdir(parents=True)

    cmake_build(
        sources["SDL_ttf"] / "external" / "freetype", builds / "freetype", prefix, sdk,
        ["-DBUILD_SHARED_LIBS=OFF", "-DFT_DISABLE_ZLIB=ON", "-DFT_DISABLE_BZIP2=ON",
         "-DFT_DISABLE_PNG=ON", "-DFT_DISABLE_BROTLI=ON", "-DFT_DISABLE_HARFBUZZ=ON"],
    )
    cmake_build(
        sources["SDL"], builds / "SDL", prefix, sdk,
        ["-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
         "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL=ON"],
    )
    cmake_build(
        sources["SDL_image"], builds / "SDL_image", prefix, sdk,
        ["-DBUILD_SHARED_LIBS=OFF", "-DSDLIMAGE_INSTALL=ON", "-DSDLIMAGE_STRICT=ON",
         f"-DSDL3_DIR={prefix / 'lib' / 'cmake' / 'SDL3'}",
         "-DSDLIMAGE_VENDORED=OFF", "-DSDLIMAGE_SAMPLES=OFF", "-DSDLIMAGE_TESTS=OFF",
         "-DSDLIMAGE_ANI=OFF", "-DSDLIMAGE_AVIF=OFF", "-DSDLIMAGE_BMP=OFF",
         "-DSDLIMAGE_GIF=OFF", "-DSDLIMAGE_JPG=OFF", "-DSDLIMAGE_LBM=OFF",
         "-DSDLIMAGE_PCX=OFF", "-DSDLIMAGE_PNG=OFF", "-DSDLIMAGE_PNM=OFF",
         "-DSDLIMAGE_QOI=OFF", "-DSDLIMAGE_SVG=ON", "-DSDLIMAGE_TGA=OFF",
         "-DSDLIMAGE_TIF=OFF", "-DSDLIMAGE_WEBP=OFF", "-DSDLIMAGE_XCF=OFF",
         "-DSDLIMAGE_XPM=OFF", "-DSDLIMAGE_XV=OFF"],
    )
    cmake_build(
        sources["SDL_ttf"], builds / "SDL_ttf", prefix, sdk,
        ["-DBUILD_SHARED_LIBS=OFF", "-DSDLTTF_INSTALL=ON", "-DSDLTTF_STRICT=ON",
         f"-DSDL3_DIR={prefix / 'lib' / 'cmake' / 'SDL3'}",
         f"-DFREETYPE_LIBRARY={prefix / 'lib' / 'libfreetype.a'}",
         f"-DFREETYPE_INCLUDE_DIR_ft2build={prefix / 'include' / 'freetype2'}",
         f"-DFREETYPE_INCLUDE_DIR_freetype2={prefix / 'include' / 'freetype2'}",
         "-DSDLTTF_VENDORED=OFF", "-DSDLTTF_SAMPLES=OFF", "-DSDLTTF_HARFBUZZ=OFF",
         "-DSDLTTF_PLUTOSVG=OFF"],
    )
    build_bzip2(sources["bzip2"], builds / "bzip2", prefix, sdk)
    build_ffmpeg(sources["ffmpeg"], builds / "ffmpeg", prefix, sdk)
    marker.write_text(json.dumps(fingerprint, indent=2, sort_keys=True) + "\n")
    return prefix


def android_llvm_tools(sdk: Path) -> Path:
    ndk = sdk / "ndk" / NDK_VERSION
    host = "linux-x86_64" if platform.system() == "Linux" else "darwin-x86_64"
    return ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin"


def build_bzip2(source: Path, build: Path, prefix: Path, sdk: Path) -> None:
    tools = android_llvm_tools(sdk)
    compiler = tools / f"aarch64-linux-android{ANDROID_API}-clang"
    if not compiler.exists():
        refuse(f"NDK compiler is missing: {compiler}")
    build.mkdir(parents=True)
    objects: list[Path] = []
    for name in ("blocksort", "huffman", "crctable", "randtable", "compress", "decompress", "bzlib"):
        output = build / f"{name}.o"
        run([
            str(compiler), "-O2", "-fPIC", "-D_FILE_OFFSET_BITS=64", "-I", str(source),
            "-c", str(source / f"{name}.c"), "-o", str(output),
        ])
        objects.append(output)
    library = prefix / "lib" / "libbz2.a"
    run([str(tools / "llvm-ar"), "rcs", str(library), *(str(path) for path in objects)])
    shutil.copy2(source / "bzlib.h", prefix / "include" / "bzlib.h")


def build_ffmpeg(source: Path, build: Path, prefix: Path, sdk: Path) -> None:
    tools = android_llvm_tools(sdk)
    compiler = tools / f"aarch64-linux-android{ANDROID_API}-clang"
    if not compiler.exists():
        refuse(f"NDK compiler is missing: {compiler}")
    build.mkdir(parents=True)
    configure = [
        str(source / "configure"), f"--prefix={prefix}", "--target-os=android", "--arch=aarch64",
        "--enable-cross-compile", f"--cc={compiler}", f"--cxx={compiler}++", f"--ar={tools / 'llvm-ar'}",
        f"--nm={tools / 'llvm-nm'}", f"--ranlib={tools / 'llvm-ranlib'}", f"--strip={tools / 'llvm-strip'}",
        "--enable-static", "--disable-shared", "--enable-pic", "--disable-programs", "--disable-doc",
        "--disable-debug", "--disable-network", "--disable-autodetect", "--disable-everything",
        "--enable-avformat", "--enable-avcodec", "--enable-avutil", "--enable-swresample",
        "--enable-protocol=file", "--enable-demuxer=asf",
        "--enable-decoder=wmav1,wmav2,wmapro,wmavoice",
    ]
    run(configure, cwd=build)
    run(["make", "-j", str(os.cpu_count() or 1)], cwd=build)
    run(["make", "install"], cwd=build)


def generate_recompiled_source(work: Path) -> Path:
    build = work / "host-tools"
    run([
        "cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
        "-DLF2_HOST_TOOLS_ONLY=ON",
    ])
    run(["cmake", "--build", str(build), "--target", "lift", "--parallel", str(os.cpu_count() or 1)])
    generated = work / "generated" / "lf2_recomp.c"
    generated.parent.mkdir(parents=True, exist_ok=True)
    run([str(build / "lift"), str(ROOT / "game" / "lf2.exe"), str(ROOT / "re" / "entries.tsv"), str(generated)])
    return generated


def build_native(work: Path, prefix: Path, generated: Path, sdk: Path) -> Path:
    build = work / "native"
    toolchain = sdk / "ndk" / NDK_VERSION / "build" / "cmake" / "android.toolchain.cmake"
    run([
        "cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DANDROID_ABI={ANDROID_ABI}",
        f"-DANDROID_PLATFORM=android-{ANDROID_API}", "-DANDROID_STL=c++_shared",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={prefix}",
        f"-DSDL3_DIR={prefix / 'lib' / 'cmake' / 'SDL3'}",
        f"-DSDL3_image_DIR={prefix / 'lib' / 'cmake' / 'SDL3_image'}",
        f"-DSDL3_ttf_DIR={prefix / 'lib' / 'cmake' / 'SDL3_ttf'}",
        f"-DFREETYPE_LIBRARY={prefix / 'lib' / 'libfreetype.a'}",
        f"-DFREETYPE_INCLUDE_DIR_ft2build={prefix / 'include' / 'freetype2'}",
        f"-DFREETYPE_INCLUDE_DIR_freetype2={prefix / 'include' / 'freetype2'}",
        f"-DBZIP2_INCLUDE_DIR={prefix / 'include'}",
        f"-DBZIP2_LIBRARY_RELEASE={prefix / 'lib' / 'libbz2.a'}",
        f"-DLF2_RECOMP_SOURCE={generated}", f"-DLF2_FFMPEG_ROOT={prefix}",
    ])
    run(["cmake", "--build", str(build), "--target", "lf2", "--parallel", str(os.cpu_count() or 1)])
    library = build / "libmain.so"
    if not library.is_file():
        refuse(f"native build did not create {library}")
    readelf_candidates = list(
        (sdk / "ndk" / NDK_VERSION / "toolchains" / "llvm" / "prebuilt").glob("*/bin/llvm-readelf")
    )
    if len(readelf_candidates) != 1:
        refuse(f"expected one NDK llvm-readelf, found {len(readelf_candidates)}")
    readelf = readelf_candidates[0]
    symbols = subprocess.run([str(readelf), "--dyn-syms", str(library)], text=True, capture_output=True, check=True)
    if not any(line.split()[-1:] == ["main"] for line in symbols.stdout.splitlines()):
        refuse(f"{library} does not export Android entrypoint main")
    return library


def cxx_shared_library(sdk: Path) -> Path:
    roots = list((sdk / "ndk" / NDK_VERSION / "toolchains" / "llvm" / "prebuilt").glob("*/sysroot/usr/lib"))
    if len(roots) != 1:
        refuse(f"expected one NDK LLVM sysroot, found {len(roots)}")
    library = roots[0] / "aarch64-linux-android" / "libc++_shared.so"
    if not library.is_file():
        refuse(f"NDK C++ runtime is missing: {library}")
    return library


def assemble_project(work: Path, sources: dict[str, Path], prefix: Path, native: Path, sdk: Path) -> Path:
    project = work / "project"
    scoped_clean(project)
    shutil.copytree(sources["SDL"] / "android-project", project)
    shutil.copy2(ROOT / "platforms" / "android" / "build.gradle", project / "build.gradle")
    shutil.copy2(
        ROOT / "platforms" / "android" / "gradle-wrapper.properties",
        project / "gradle" / "wrapper" / "gradle-wrapper.properties",
    )
    shutil.copytree(ROOT / "platforms" / "android" / "app", project / "app", dirs_exist_ok=True)
    lucent_java = ROOT / "third_party" / "lucent" / "platforms" / "android" / "java"
    if lucent_java.is_dir():
        shutil.copytree(lucent_java, project / "app" / "src" / "main" / "java", dirs_exist_ok=True)
    assets = project / "app" / "src" / "main" / "assets"
    shutil.copytree(ROOT / "stages", assets / "stages")
    libraries = project / "app" / "src" / "main" / "jniLibs" / ANDROID_ABI
    libraries.mkdir(parents=True)
    shutil.copy2(prefix / "lib" / "libSDL3.so", libraries / "libSDL3.so")
    shutil.copy2(native, libraries / "libmain.so")
    shutil.copy2(cxx_shared_library(sdk), libraries / "libc++_shared.so")
    return project


def inspect_apk(apk: Path) -> None:
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
    game_directories = {"game", "data", "sprite", "bg", "bgm", "music", "sound"}
    forbidden = [
        name for name in names
        if Path(name).name.lower() in {"lf2.exe", "lf2_v2.0a.exe", "data.txt"}
        or game_directories.intersection(part.lower() for part in Path(name).parts)
    ]
    required = [f"lib/{ANDROID_ABI}/libmain.so", f"lib/{ANDROID_ABI}/libSDL3.so",
                f"lib/{ANDROID_ABI}/libc++_shared.so", "res/drawable/lf2_port_icon.xml"]
    missing = [name for name in required if name not in names]
    if forbidden:
        refuse("APK contains prohibited original game paths: " + ", ".join(forbidden))
    if missing:
        refuse("APK is missing native libraries: " + ", ".join(missing))


def build_apk(project: Path, sdk: Path, java: Path, signing: Mapping[str, str] | None,
              release: bool) -> Path:
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
    run([str(project / "gradlew"), "--no-daemon", f"-Dorg.gradle.java.home={java}", task],
        cwd=project, environment=environment)
    kind = "release" if release else "debug"
    candidates = sorted((project / "app" / "build" / "outputs" / "apk" / kind).glob("*.apk"))
    if len(candidates) != 1:
        refuse(f"expected one {kind} APK, found {len(candidates)}")
    inspect_apk(candidates[0])
    output = ROOT / "scratch" / "releases" / f"LF2-Port-{version}-android-arm64-{kind}.apk"
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(candidates[0], output)
    if release:
        run([str(apksigner(sdk)), "verify", "--verbose", "--print-certs", str(output)])
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"android build: {output}\nandroid build: sha256 {digest}")
    return output


def connected_device(adb: Path) -> bool:
    result = subprocess.run([str(adb), "devices"], text=True, capture_output=True, check=True)
    devices = [line for line in result.stdout.splitlines()[1:] if line.rstrip().endswith("\tdevice")]
    if len(devices) != 1:
        refuse(f"--install requires exactly one connected device, found {len(devices)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--release", action="store_true", help="produce a signed release APK")
    parser.add_argument("--native-only", action="store_true", help="build and inspect libmain.so without Gradle")
    parser.add_argument("--install", action="store_true", help="install and launch on exactly one adb device")
    args = parser.parse_args()

    for name, hint in (
        ("git", "sudo dnf install git"), ("cmake", "sudo dnf install cmake"),
        ("ninja", "sudo dnf install ninja-build"), ("make", "sudo dnf install make"),
        ("clang", "sudo dnf install clang"), ("clang++", "sudo dnf install clang"),
    ):
        require_program(name, hint)
    if not (ROOT / "game" / "lf2.exe").is_file():
        refuse("game/lf2.exe is required locally to generate the translated source; it is never packaged or committed")
    sdk = android_sdk()
    java = None if args.native_only else java_home()
    signing = release_signing() if args.release and not args.native_only else None
    work = args.work_dir.resolve()
    if args.clean:
        scoped_clean(work)
    work.mkdir(parents=True, exist_ok=True)

    sources = prepare_sources(work)
    prefix = build_dependencies(work, sources, sdk)
    generated = generate_recompiled_source(work)
    native = build_native(work, prefix, generated, sdk)
    if args.native_only:
        print(f"android build: native library passed entrypoint inspection: {native}")
        return 0
    assert java is not None
    project = assemble_project(work, sources, prefix, native, sdk)
    apk = build_apk(project, sdk, java, signing, args.release)
    if args.install:
        adb = sdk / "platform-tools" / "adb"
        connected_device(adb)
        run([str(adb), "install", "-r", str(apk)])
        run([str(adb), "shell", "am", "start", "-n",
             "io.github.someoneisworking.lf2port/.Lf2Activity"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
