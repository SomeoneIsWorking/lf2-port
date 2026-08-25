#!/usr/bin/env python3
"""Initialize everything this port needs that is not in the repo, then run.

    ./bootstrap.py [ARGUMENTS FOR THE GAME...]

A fresh clone plus the installer should be enough. What used to be setup
paragraphs -- the shared port-assets checkout, the RmlUi and Lucent submodules, the
Python environment, the extracted game tree -- is provisioned here or refused
BY NAME with the exact command that would fix it. Nothing is fetched
silently: every step prints what it did.

Order matters and each step needs only the ones before it:

  1. port-assets          validates $PORT_ASSETS_DIR without touching it;
                          otherwise reuses $SHARED_DIR/port-assets or the
                          established ../../shared checkout when available,
                          and clones a pinned copy into scratch/deps as the
                          portable fallback.
  2. source submodules    initializes pinned RmlUi and Lucent checkouts when absent.
  3. Python environment   `uv sync --frozen`, from the committed lockfile. uv
                          provisions Python itself if the system one is old.
  4. game tree            extracted from the installer with
                          tools/extract_game.py (no Windows or Wine needed).
                          The installer is taken from $LF2_INSTALLER, then
                          ./LF2_v2.0a.exe; if neither exists it is downloaded
                          from lf2.net, where the README already points.
  5. build                tools/build/build.py (cmake; skipped when the
                          binary is newer than every input -- REBUILD=1
                          forces it).
  6. run                  the game, from the game tree (its data opens by
                          relative path), with your arguments passed through.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INSTALLER_URL = "https://lf2.net/LF2_v2.0a.exe"
INSTALLER = "LF2_v2.0a.exe"
GAME_EXE = "game/lf2.exe"
BUILD_DIR = ROOT / "scratch" / "build-clang"
BINARY = BUILD_DIR / "lf2"
PORT_ASSET_FILES = (
    Path("sets/devices/keyboard.svg"),
    Path("sets/devices/gamepad.svg"),
)
PORT_ASSETS_REVISION = "330c1bf146bd97ecc6af49e637476a677f0e55a0"
SUBMODULE_INPUTS = (
    Path("third_party/RmlUi/CMakeLists.txt"),
    Path("third_party/lucent/CMakeLists.txt"),
)


def refuse(message: str) -> None:
    sys.exit(f"bootstrap: {message}")


def configured_path(value: str) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else ROOT / path


def missing_port_assets(target: Path) -> list[Path]:
    return [
        relative for relative in PORT_ASSET_FILES if not (target / relative).is_file()
    ]


def ensure_port_assets() -> Path:
    """Resolve or provision shared device art without assuming checkout layout.

    An explicit $PORT_ASSETS_DIR is user-owned and must already be complete.
    A shared-root or bootstrap-owned checkout may be cloned when absent; no
    existing checkout is pulled or otherwise changed.
    """
    explicit = os.environ.get("PORT_ASSETS_DIR")
    if explicit:
        target = configured_path(explicit)
        missing = missing_port_assets(target)
        if missing:
            refuse(
                "PORT_ASSETS_DIR={} is missing required file(s): {}.".format(
                    target, ", ".join(str(path) for path in missing)
                )
            )
        return target.resolve()

    shared = os.environ.get("SHARED_DIR")
    if shared:
        target = configured_path(shared) / "port-assets"
    else:
        legacy = ROOT.parent.parent / "shared" / "port-assets"
        if not missing_port_assets(legacy):
            return legacy.resolve()
        target = ROOT / "scratch" / "deps" / "port-assets"

    missing = missing_port_assets(target)
    if not missing:
        return target.resolve()
    if target.exists():
        refuse(
            "{} exists but is missing required port-assets file(s): {}. "
            "Fix or remove it and re-run.".format(
                target, ", ".join(str(path) for path in missing)
            )
        )
    git = shutil.which("git")
    if not git:
        refuse("git is required to fetch the shared port-assets checkout.")
    base = os.environ.get("SHARED_GIT_BASE") or "https://github.com/SomeoneIsWorking"
    url = f"{base}/port-assets.git"
    target.parent.mkdir(parents=True, exist_ok=True)
    print(f"bootstrap: cloning {url} -> {target}")
    result = subprocess.run([git, "clone", url, str(target)], check=False)
    if result.returncode:
        refuse(
            f"the shared port-assets checkout did not initialize at {target}; see the output above."
        )
    print(f"bootstrap: pinning port-assets to {PORT_ASSETS_REVISION}")
    result = subprocess.run(
        [git, "-C", str(target), "checkout", "--detach", PORT_ASSETS_REVISION],
        check=False,
    )
    if result.returncode or missing_port_assets(target):
        refuse(
            f"the shared port-assets checkout did not initialize at {target}; see the output above."
        )
    return target.resolve()


def ensure_submodules() -> None:
    missing = [path for path in SUBMODULE_INPUTS if not (ROOT / path).is_file()]
    if not missing:
        return
    git = shutil.which("git")
    if not git:
        refuse(
            "git is required to fetch missing submodule(s): {}.".format(
                ", ".join(str(path.parent) for path in missing)
            )
        )
    print("bootstrap: git submodule update --init --recursive")
    result = subprocess.run(
        [git, "submodule", "update", "--init", "--recursive"],
        cwd=ROOT,
        check=False,
    )
    missing = [path for path in SUBMODULE_INPUTS if not (ROOT / path).is_file()]
    if result.returncode or missing:
        refuse(
            "submodule initialization failed for: {}; see the output above.".format(
                ", ".join(str(path.parent) for path in missing)
            )
        )


def ensure_venv() -> Path:
    """`uv sync --frozen` from the committed lockfile; return the venv interpreter."""
    uv = shutil.which("uv")
    if not uv:
        refuse(
            "uv is required to manage this port's Python dependencies.\n"
            "         Install it with one of:\n"
            "             curl -LsSf https://astral.sh/uv/install.sh | sh\n"
            "             brew install uv"
        )
    print("bootstrap: uv sync --frozen")
    result = subprocess.run([uv, "sync", "--frozen"], cwd=ROOT, check=False)
    if result.returncode:
        refuse(f"uv sync --frozen failed with exit {result.returncode}.")
    venv_python = ROOT / ".venv" / "bin" / "python"
    if not venv_python.exists():
        refuse(f"{venv_python} does not exist after uv sync --frozen.")
    return venv_python


def ensure_installer() -> Path:
    """The installer: $LF2_INSTALLER, then the copy beside the port, then a
    download from lf2.net. A missing prerequisite names itself and its fix;
    it never silently skips extraction."""
    env_path = os.environ.get("LF2_INSTALLER")
    if env_path:
        candidate = Path(env_path).expanduser()
        if not candidate.is_file():
            refuse(f"LF2_INSTALLER={candidate} is not a file.")
        return candidate
    local = ROOT / INSTALLER
    if local.is_file():
        return local
    print(f"bootstrap: downloading {INSTALLER} ({INSTALLER_URL})")
    try:
        urllib.request.urlretrieve(INSTALLER_URL, local)  # pinned URL, fixed filename
    except OSError as error:
        local.unlink(missing_ok=True)
        refuse(
            f"no installer at {local} and the download failed ({error}).\n"
            f"         Fetch {INSTALLER_URL} yourself, place it at {local}, and re-run."
        )
    if not local.is_file() or local.stat().st_size == 0:
        refuse(f"the download of {INSTALLER_URL} produced nothing usable.")
    return local


def ensure_game_tree() -> None:
    if (ROOT / GAME_EXE).is_file():
        return
    installer = ensure_installer()
    print(f"bootstrap: extracting game tree from {installer.name}")
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "extract_game.py"),
            str(installer),
            str(ROOT / "game"),
        ],
        cwd=ROOT,
        check=False,
    )
    if result.returncode or not (ROOT / GAME_EXE).is_file():
        refuse(
            f"tools/extract_game.py did not produce {GAME_EXE}; see its output above."
        )


def build(venv_python: Path, port_assets: Path) -> None:
    if BINARY.exists() and not os.environ.get("REBUILD"):
        inputs_newest = newest_mtime(
            ROOT / "runtime",
            ROOT / "recompiler",
            ROOT / "re",
            ROOT / "assets",
            ROOT / "stages",
            ROOT / "third_party",
            ROOT / "tools" / "build",
            ROOT / GAME_EXE,
            ROOT / "CMakeLists.txt",
            port_assets,
        )
        if inputs_newest <= BINARY.stat().st_mtime:
            return
        print("bootstrap: sources changed since the last build; rebuilding")
    env = dict(os.environ)
    env["PATH"] = "{}{}{}".format(venv_python.parent, os.pathsep, env.get("PATH", ""))
    env["PORT_ASSETS_DIR"] = str(port_assets)
    result = subprocess.run(
        [str(venv_python), str(ROOT / "tools" / "build" / "build.py")],
        cwd=ROOT,
        env=env,
        check=False,
    )
    if result.returncode:
        refuse(f"the build failed with exit {result.returncode}.")
    if not BINARY.exists():
        refuse(f"the build reported success but {BINARY} does not exist.")


def newest_mtime(*paths: Path) -> float:
    newest = 0.0
    for path in paths:
        if path.is_file():
            newest = max(newest, path.stat().st_mtime)
        elif path.is_dir():
            newest = max(
                newest,
                max(
                    (p.stat().st_mtime for p in path.rglob("*") if p.is_file()),
                    default=0.0,
                ),
            )
    return newest


def main(argv: list[str]) -> int:
    os.chdir(ROOT)
    print(f"bootstrap: initializing {ROOT}")
    port_assets = ensure_port_assets()
    ensure_submodules()
    venv_python = ensure_venv()
    ensure_game_tree()
    build(venv_python, port_assets)

    # Run from the game tree: the game opens its data by relative path.
    # The venv goes on PATH so helper scripts' bare `python3` lands on the
    # interpreter with this port's dependencies installed.
    env = dict(os.environ)
    env["PATH"] = "{}{}{}".format(venv_python.parent, os.pathsep, env.get("PATH", ""))
    env["VIRTUAL_ENV"] = str(ROOT / ".venv")
    print("bootstrap: starting the game (env switches: docs/running.md)")
    # execve discards this process image -- and its buffered stdout with it.
    sys.stdout.flush()
    sys.stderr.flush()
    os.chdir(ROOT / "game")
    os.execve(str(BINARY), [str(BINARY), "lf2.exe", *argv], env)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
