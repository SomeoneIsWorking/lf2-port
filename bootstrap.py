#!/usr/bin/env python3
"""Initialize everything this port needs that is not in the repo, then run.

    ./bootstrap.py [ARGUMENTS FOR THE GAME...]

A fresh clone plus the installer should be enough. What used to be setup
paragraphs -- the shared port-assets checkout, the RmlUi submodule, the
Python environment, the extracted game tree -- is provisioned here or refused
BY NAME with the exact command that would fix it. Nothing is fetched
silently: every step prints what it did.

Order matters and each step needs only the ones before it:

  1. shared/port-assets   cloned into the standard layout (`shared/` beside
                          `pc/`) when missing; an existing checkout is left
                          exactly as it is.
  2. third_party/RmlUi    `git submodule update --init` when absent.
  3. Python environment   `uv sync`, from the committed lockfile. uv
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


def refuse(message: str) -> None:
    sys.exit("bootstrap: %s" % message)


def ensure_port_assets() -> None:
    """Clone the shared device-art repo into the standard layout when missing.

    CMake embeds SVGs from it at build time and resolves exactly this path
    ($PORT_ASSETS_DIR / $SHARED_DIR / ../../shared/port-assets); provisioning
    the standard location means no variable needs to be set. An existing
    checkout is never pulled or touched.
    """
    target = ROOT.parent.parent / "shared" / "port-assets"
    if (target / "sets").exists():
        return
    if target.exists():
        refuse(
            "%s exists but has no sets/ directory, so it does not look like "
            "a port-assets checkout. Fix or remove it and re-run." % target
        )
    base = os.environ.get("SHARED_GIT_BASE") or "https://github.com/SomeoneIsWorking"
    url = "%s/port-assets.git" % base
    target.parent.mkdir(parents=True, exist_ok=True)
    print("bootstrap: cloning %s -> %s" % (url, target))
    subprocess.run(["git", "clone", url, str(target)], check=True)
    if not (target / "sets").exists():
        refuse("%s was cloned from %s but has no sets/" % (target, url))


def ensure_submodules() -> None:
    if (ROOT / "third_party" / "RmlUi" / "CMakeLists.txt").exists():
        return
    git = shutil.which("git")
    if not git:
        refuse("git is required to fetch the third_party/RmlUi submodule.")
    print("bootstrap: git submodule update --init --recursive")
    result = subprocess.run([git, "submodule", "update", "--init", "--recursive"], cwd=ROOT)
    if result.returncode or not (ROOT / "third_party" / "RmlUi" / "CMakeLists.txt").exists():
        refuse("the RmlUi submodule did not initialize; see the output above.")


def ensure_venv() -> Path:
    """`uv sync` from the committed lockfile; return the venv interpreter."""
    uv = shutil.which("uv")
    if not uv:
        refuse(
            "uv is required to manage this port's Python dependencies.\n"
            "         Install it with one of:\n"
            "             curl -LsSf https://astral.sh/uv/install.sh | sh\n"
            "             brew install uv"
        )
    print("bootstrap: uv sync")
    result = subprocess.run([uv, "sync"], cwd=ROOT)
    if result.returncode:
        refuse("uv sync failed with exit %d." % result.returncode)
    venv_python = ROOT / ".venv" / "bin" / "python"
    if not venv_python.exists():
        refuse("%s does not exist after uv sync." % venv_python)
    return venv_python


def ensure_installer() -> Path:
    """The installer: $LF2_INSTALLER, then the copy beside the port, then a
    download from lf2.net. A missing prerequisite names itself and its fix;
    it never silently skips extraction."""
    env_path = os.environ.get("LF2_INSTALLER")
    if env_path:
        candidate = Path(env_path).expanduser()
        if not candidate.is_file():
            refuse("LF2_INSTALLER=%s is not a file." % candidate)
        return candidate
    local = ROOT / INSTALLER
    if local.is_file():
        return local
    print("bootstrap: downloading %s (%s)" % (INSTALLER, INSTALLER_URL))
    try:
        urllib.request.urlretrieve(INSTALLER_URL, local)  # pinned URL, fixed filename
    except OSError as error:
        local.unlink(missing_ok=True)
        refuse(
            "no installer at %s and the download failed (%s).\n"
            "         Fetch %s yourself, place it at %s, and re-run."
            % (local, error, INSTALLER_URL, local)
        )
    if not local.is_file() or local.stat().st_size == 0:
        refuse("the download of %s produced nothing usable." % INSTALLER_URL)
    return local


def ensure_game_tree(installer: Path) -> None:
    if (ROOT / GAME_EXE).is_file():
        return
    print("bootstrap: extracting game tree from %s" % installer.name)
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "extract_game.py"),
                             str(installer), str(ROOT / "game")], cwd=ROOT)
    if result.returncode or not (ROOT / GAME_EXE).is_file():
        refuse("tools/extract_game.py did not produce %s; see its output above." % GAME_EXE)


def build(venv_python: Path) -> None:
    if BINARY.exists() and not os.environ.get("REBUILD"):
        inputs_newest = newest_mtime(ROOT / "runtime", ROOT / "recompiler", ROOT / "CMakeLists.txt")
        if inputs_newest <= BINARY.stat().st_mtime:
            return
        print("bootstrap: sources changed since the last build; rebuilding")
    env = dict(os.environ)
    env["PATH"] = "%s%s%s" % (venv_python.parent, os.pathsep, env.get("PATH", ""))
    result = subprocess.run([str(venv_python), str(ROOT / "tools" / "build" / "build.py")],
                            cwd=ROOT, env=env)
    if result.returncode:
        refuse("the build failed with exit %d." % result.returncode)
    if not BINARY.exists():
        refuse("the build reported success but %s does not exist." % BINARY)


def newest_mtime(*paths: Path) -> float:
    newest = 0.0
    for path in paths:
        if path.is_file():
            newest = max(newest, path.stat().st_mtime)
        elif path.is_dir():
            newest = max(newest, max((p.stat().st_mtime for p in path.rglob("*") if p.is_file()),
                                     default=0.0))
    return newest


def main(argv: list[str]) -> int:
    os.chdir(ROOT)
    print("bootstrap: initializing %s" % ROOT)
    ensure_port_assets()
    ensure_submodules()
    venv_python = ensure_venv()
    ensure_game_tree(ensure_installer())
    build(venv_python)

    # Run from the game tree: the game opens its data by relative path.
    # The venv goes on PATH so helper scripts' bare `python3` lands on the
    # interpreter with this port's dependencies installed.
    env = dict(os.environ)
    env["PATH"] = "%s%s%s" % (venv_python.parent, os.pathsep, env.get("PATH", ""))
    env["VIRTUAL_ENV"] = str(ROOT / ".venv")
    print("bootstrap: starting the game (env switches: docs/running.md)")
    # execve discards this process image -- and its buffered stdout with it.
    sys.stdout.flush()
    sys.stderr.flush()
    os.chdir(ROOT / "game")
    os.execve(str(BINARY), [str(BINARY), "lf2.exe", *argv], env)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
