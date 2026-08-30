#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import shutil
import sys
import zipfile
from collections.abc import Callable
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))
SPEC = importlib.util.spec_from_file_location("lf2_android", ROOT / "tools" / "build" / "android.py")
assert SPEC and SPEC.loader
ANDROID = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANDROID)


def expect_refused(operation: Callable[[], object], needle: str) -> None:
    try:
        operation()
    except SystemExit as error:
        if needle not in str(error):
            raise AssertionError(f"expected {needle!r} in {error!r}") from error
    else:
        raise AssertionError(f"operation accepted input that should be refused: {needle}")


def make_apk(path: Path, names: list[str]) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        for name in names:
            archive.writestr(name, b"test")


def main() -> int:
    assert not ANDROID.supported_java_major(16)
    assert ANDROID.supported_java_major(17)
    assert ANDROID.supported_java_major(25)
    assert ANDROID.supported_java_major(26)
    assert not ANDROID.supported_java_major(27)

    wrapper = ROOT / "platforms" / "android" / "gradle-wrapper.properties"
    wrapper_text = wrapper.read_text()
    assert "gradle-9.7.1-bin.zip" in wrapper_text
    assert "distributionSha256Sum=acd53f1edaf02f1a8ff99879f8a34b302661a057d9b063ae9e35b552f804d20a" in wrapper_text
    assert "com.android.tools.build:gradle:9.3.0" in (
        ROOT / "platforms" / "android" / "build.gradle"
    ).read_text()

    scratch = ROOT / "scratch" / "test-android-tool"
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)

    expect_refused(lambda: ANDROID.release_signing({}), "Refusing an unsigned APK")
    keystore = scratch / "release.jks"
    keystore.write_bytes(b"not-a-real-keystore")
    signing = ANDROID.release_signing(
        {
            "LF2_ANDROID_KEYSTORE": str(keystore),
            "LF2_ANDROID_KEY_ALIAS": "release",
            "LF2_ANDROID_STORE_PASSWORD": "store-secret",
            "LF2_ANDROID_KEY_PASSWORD": "key-secret",
        }
    )
    assert signing["LF2_ANDROID_KEYSTORE"] == str(keystore.resolve())

    required = [
        "lib/arm64-v8a/libmain.so",
        "lib/arm64-v8a/libSDL3.so",
        "lib/arm64-v8a/libc++_shared.so",
        "res/drawable/lf2_port_icon.xml",
    ]
    clean_apk = scratch / "clean.apk"
    make_apk(clean_apk, required)
    ANDROID.inspect_apk(clean_apk)

    game_apk = scratch / "contains-game.apk"
    make_apk(game_apk, [*required, "assets/game/data/data.txt"])
    expect_refused(lambda: ANDROID.inspect_apk(game_apk), "prohibited original game paths")

    incomplete_apk = scratch / "incomplete.apk"
    make_apk(incomplete_apk, required[:-1])
    expect_refused(lambda: ANDROID.inspect_apk(incomplete_apk), "lf2_port_icon.xml")

    shutil.rmtree(scratch)
    print("android tool: signing and APK content gates passed positive and negative cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
