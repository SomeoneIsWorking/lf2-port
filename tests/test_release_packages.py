#!/usr/bin/env python3
"""Exercise the real release context and package collector with both outcomes."""

from __future__ import annotations

import hashlib
import importlib.util
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name: str, path: Path):
    specification = importlib.util.spec_from_file_location(name, path)
    assert specification and specification.loader
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


CONTEXT = load("lf2_release_context", ROOT / "tools/build/release_context.py")
MANIFEST = load("lf2_release_manifest", ROOT / "tools/build/release_manifest.py")


def refused(operation, expected: str) -> None:
    try:
        operation()
    except ValueError as error:
        assert expected in str(error), (expected, str(error))
    else:
        raise AssertionError(f"accepted release input that should be refused: {expected}")


def main() -> int:
    assert CONTEXT.release_context(
        event="push", ref_type="tag", ref_name="v0.1.10", input_tag="", publish=False
    ) == ("v0.1.10", "0.1.10", True)
    assert CONTEXT.release_context(
        event="workflow_dispatch",
        ref_type="branch",
        ref_name="main",
        input_tag="",
        publish=False,
    ) == ("", "0.0.0", False)
    assert CONTEXT.release_context(
        event="workflow_dispatch",
        ref_type="tag",
        ref_name="v0.1.10",
        input_tag="v0.1.10",
        publish=True,
    ) == ("v0.1.10", "0.1.10", True)
    refused(
        lambda: CONTEXT.release_context(
            event="workflow_dispatch",
            ref_type="branch",
            ref_name="main",
            input_tag="v0.1.9",
            publish=True,
        ),
        "tag ref",
    )
    refused(
        lambda: CONTEXT.release_context(
            event="workflow_dispatch", ref_type="tag", ref_name="v0.1.10", input_tag="v0.1.9", publish=True
        ),
        "differs",
    )

    build = ROOT / "build" / "test-release-packages"
    if build.exists():
        shutil.rmtree(build)
    dist = build / "dist"
    output = build / "release_assets"
    dist.mkdir(parents=True)
    try:
        expected = MANIFEST.expected_names("0.1.10")
        for name in expected:
            (dist / name).write_bytes(name.encode("ascii"))
        MANIFEST.collect(dist, output, "0.1.10")
        lines = (output / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
        assert len(lines) == len(expected)
        for name in expected:
            assert (output / name).is_file()
            digest = hashlib.sha256(name.encode("ascii")).hexdigest()
            assert f"{digest}  {name}" in lines
        (dist / "LF2-Port-macos-arm64.zip").unlink()
        refused(lambda: MANIFEST.collect(dist, output, "0.1.10"), "macos-arm64.zip")
        (dist / "LF2-Port-macos-arm64.zip").write_bytes(b"zip")
        (dist / "LF2-Port-0.1.10-android-arm64-debug.apk").write_bytes(b"debug")
        refused(lambda: MANIFEST.collect(dist, output, "0.1.10"), "debug.apk")
    finally:
        shutil.rmtree(build)
    print("release packages: ref, artifact set, and checksum discriminators passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
