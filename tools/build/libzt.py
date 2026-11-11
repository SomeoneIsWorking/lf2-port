#!/usr/bin/env python3
"""Provision libzt, the embedded ZeroTier node lucent::zerotier links, once per revision and compiler.

The install goes to a shared prefix outside the repo, the one Lucent's own tree uses, so every
consumer built with the same compiler reuses it; CMake finds it as `libzt::zt`.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

REPOSITORY = "https://github.com/SomeoneIsWorking/libzt.git"
REVISION = "6e74b6510af9bf00469f329075a17f6622edf132"


class LibztError(RuntimeError):
    """libzt could not be provisioned."""


def compiler_tag(cxx: str) -> str:
    result = subprocess.run([cxx, "--version"], capture_output=True, text=True, check=False)
    first = result.stdout.splitlines()[0] if result.stdout else ""
    match = re.search(r"(clang|g\+\+|gcc)[^0-9]*([0-9]+(?:\.[0-9]+)*)", first)
    if result.returncode or not match:
        raise LibztError(f"cannot read the version of {cxx}: {first or result.stderr.strip()}")
    name = "clang" if match.group(1) == "clang" else "gcc"
    return f"{name}{match.group(2)}"


def prefix_root() -> Path:
    data = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(data) / "lucent-thirdparty"


def provision(cc: str, cxx: str) -> Path:
    prefix = prefix_root() / f"libzt-{REVISION[:7]}-{compiler_tag(cxx)}"
    if (prefix / "lib" / "cmake").is_dir():
        return prefix
    git = shutil.which("git")
    if git is None:
        raise LibztError("git is required to provision libzt; install it with `sudo dnf install git`, "
                         "`sudo apt install git`, or `brew install git`")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".libzt-", dir=prefix.parent) as raw:
        work = Path(raw)
        source, build, staged = work / "source", work / "build", work / "prefix"
        # Only the pinned commit: smaller, and a full clone of this repository fails over HTTP/2.
        run([git, "init", "-q", str(source)])
        run([git, "fetch", "-q", "--depth", "1", REPOSITORY, REVISION], source)
        run([git, "checkout", "-q", "--detach", "FETCH_HEAD"], source)
        run([git, "submodule", "update", "-q", "--init", "--recursive", "--depth", "1"], source)
        run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}", "-DBUILD_SHARED_LIB=OFF",
             "-DBUILD_HOST_SELFTEST=OFF", f"-DCMAKE_INSTALL_PREFIX={staged}"])
        run(["cmake", "--build", str(build), "--parallel", str(os.cpu_count() or 1)])
        run(["cmake", "--install", str(build)])
        if prefix.exists():
            raise LibztError(f"{prefix} appeared during provisioning; refusing to overwrite it")
        staged.replace(prefix)
    return prefix


def run(command: list[str], cwd: Path | None = None) -> None:
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode:
        raise LibztError(f"{' '.join(command)} failed with exit code {result.returncode}")
