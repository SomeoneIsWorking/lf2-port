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
    assert ANDROID.android_version_name({}) == "0.1.0"
    assert ANDROID.android_version_name({"LF2_ANDROID_VERSION_NAME": "0.1.3"}) == "0.1.3"
    assert ANDROID.android_version_code("0.1.3") == 1003
    assert ANDROID.android_version_code("1.0.0") == 1_000_000
    expect_refused(
        lambda: ANDROID.android_version_name({"LF2_ANDROID_VERSION_NAME": "../release"}),
        "semantic version",
    )
    expect_refused(lambda: ANDROID.android_version_code("0.0.0"), "cannot be published")

    wrapper = ROOT / "platforms" / "android" / "gradle-wrapper.properties"
    wrapper_text = wrapper.read_text()
    assert "gradle-9.7.1-bin.zip" in wrapper_text
    assert "distributionSha256Sum=acd53f1edaf02f1a8ff99879f8a34b302661a057d9b063ae9e35b552f804d20a" in wrapper_text
    assert "com.android.tools.build:gradle:9.3.0" in (
        ROOT / "platforms" / "android" / "build.gradle"
    ).read_text()
    app_gradle = (ROOT / "platforms" / "android" / "app" / "build.gradle").read_text()
    assert "enableV3Signing = true" in app_gradle
    assert "v3SigningEnabled" not in app_gradle

    manifest = (ROOT / "platforms" / "android" / "app" / "src" / "main" / "AndroidManifest.xml").read_text()
    assert 'android:screenOrientation="sensorLandscape"' in manifest
    assert 'android.permission.REQUEST_INSTALL_PACKAGES' in manifest
    assert 'android.permission.INTERNET' in manifest
    assert 'android:name="io.github.someoneisworking.lf2port.UpdateFileProvider"' in manifest
    updater = (
        ROOT / "platforms" / "android" / "app" / "src" / "main" / "java" / "io" / "github"
        / "someoneisworking" / "lf2port" / "UpdateManager.java"
    ).read_text()
    assert "SomeoneIsWorking/lf2-port/releases?per_page=20" in updater
    assert '"-android-arm64-release.apk"' in updater
    assert "APK signing certificate does not match" in updater
    window_policy = (ROOT / "runtime" / "platform" / "window_policy.c").read_text()
    assert 'SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight")' in window_policy
    bridge = (ROOT / "runtime" / "platform" / "android_bridge.c").read_text()
    assert 'call_activity_void("enforceLf2WindowPolicy"' in bridge
    assert "finishApp" not in bridge
    host_frame = (ROOT / "runtime" / "video" / "host_frame.c").read_text()
    assert "android_bridge_finish_activity" not in host_frame
    activity = (
        ROOT / "platforms" / "android" / "app" / "src" / "main" / "java" / "io" / "github"
        / "someoneisworking" / "lf2port" / "Lf2Activity.java"
    ).read_text()
    assert "enforceLf2WindowPolicy" in activity
    assert "ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE" in activity
    assert "hideSystemUI();" in activity
    assert 'android:alwaysRetainTaskState="true"' in manifest
    assert 'android:launchMode="singleInstance"' in manifest
    touch_input = (ROOT / "runtime" / "input" / "touch_input.cpp").read_text()
    assert "touch_input_note_controller_event" in touch_input
    assert "state.presentation.note_touch();" in touch_input
    settings_ui = (ROOT / "runtime" / "ui" / "settings_ui.cpp").read_text()
    assert 'data-if="quit_supported"' in settings_ui
    assert "M.quit_supported = false;" in settings_ui
    dsound = (ROOT / "runtime" / "audio" / "dsound.c").read_text()
    assert "SDL_PauseAudioStreamDevice(stream);" in dsound
    assert "SDL_ClearAudioStream(stream);" in dsound

    build = ROOT / "build" / "test-android-tool"
    if build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True)

    expect_refused(lambda: ANDROID.release_signing({}), "Refusing an unsigned APK")
    keystore = build / "release.jks"
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
        "resources.arsc",
    ]
    clean_apk = build / "clean.apk"
    make_apk(clean_apk, required)
    ANDROID.inspect_apk(clean_apk)

    game_apk = build / "contains-game.apk"
    make_apk(game_apk, [*required, "assets/game/data/data.txt"])
    expect_refused(lambda: ANDROID.inspect_apk(game_apk), "prohibited original game paths")

    incomplete_apk = build / "incomplete.apk"
    make_apk(incomplete_apk, required[:-1])
    expect_refused(lambda: ANDROID.inspect_apk(incomplete_apk), "resources.arsc")

    shutil.rmtree(build)
    print("android tool: signing and APK content gates passed positive and negative cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
