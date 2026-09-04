#!/usr/bin/env python3
"""The project honors CC/CXX and otherwise leaves compiler choice to the platform."""

from pathlib import Path
import importlib.util
import os
import tempfile
import sys
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))
SPEC = importlib.util.spec_from_file_location(
    "lf2_build", ROOT / "tools" / "build" / "build.py"
)
assert SPEC and SPEC.loader
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        cache = Path(directory) / "CMakeCache.txt"
        with mock.patch.dict(os.environ, {}, clear=True):
            assert BUILD.pick_compilers(cache) == ("cc", "c++")

        cache.write_text(
            "CMAKE_C_COMPILER:STRING=/toolchain/cached-cc\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/toolchain/cached-cxx\n"
        )
        assert BUILD.cached_compiler(cache, "C") == "/toolchain/cached-cc"
        assert BUILD.cached_compiler(cache, "CXX") == "/toolchain/cached-cxx"
        with mock.patch.dict(os.environ, {}, clear=True):
            assert BUILD.pick_compilers(cache) == (
                "/toolchain/cached-cc",
                "/toolchain/cached-cxx",
            )
        with mock.patch.dict(
            os.environ, {"CC": "chosen-c", "CXX": "chosen-cxx"}, clear=True
        ):
            assert BUILD.pick_compilers(cache) == ("chosen-c", "chosen-cxx")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
