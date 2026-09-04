#!/usr/bin/env python3
"""Resolve and provision LF2's pinned shared runtime source dependencies."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SourceDependency:
    name: str
    repository: str
    revision: str
    marker: str

    @property
    def environment_key(self) -> str:
        return f"{self.name.replace('-', '_').upper()}_DIR"


JIT_COMMON = SourceDependency(
    name="jit-common",
    repository="https://github.com/SomeoneIsWorking/jit-common.git",
    revision="75ce92882aba7d80a39822604ab3a294f9c8944e",
    marker="src/jitcommon/block_cache.h",
)

X86PORT = SourceDependency(
    name="x86port",
    repository="https://github.com/SomeoneIsWorking/x86port.git",
    revision="9b224ebdb2bfc9e60fa507cdf98b3ab389f3f814",
    marker="src/x86port/jit_engine.h",
)

RUNTIME_DEPENDENCIES = (JIT_COMMON, X86PORT)


class DependencyError(RuntimeError):
    """A pinned checkout is absent, mutable, or not the declared dependency."""


def _run_git(arguments: Sequence[str], cwd: Path | None = None) -> str:
    git = shutil.which("git")
    if git is None:
        raise DependencyError(
            "git is required to resolve the pinned JIT dependencies; install it with "
            "`sudo dnf install git`, `sudo apt install git`, or `brew install git`"
        )
    try:
        result = subprocess.run(
            [git, *arguments],
            cwd=cwd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        raise DependencyError(f"git could not run: {error}") from error
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        raise DependencyError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout.strip()


def _canonical_repository(value: str) -> str:
    normalized = value.strip().removesuffix(".git").rstrip("/")
    for prefix in (
        "https://github.com/",
        "http://github.com/",
        "ssh://git@github.com/",
        "git@github.com:",
    ):
        if normalized.startswith(prefix):
            return "github.com/" + normalized[len(prefix) :].casefold()
    try:
        return str(Path(normalized).expanduser().resolve())
    except OSError:
        return normalized


def validate_checkout(dependency: SourceDependency, checkout: Path) -> Path:
    checkout = checkout.expanduser().resolve()
    if not (checkout / ".git").exists():
        raise DependencyError(f"{checkout} exists but is not a git checkout")
    origin = _run_git(("remote", "get-url", "origin"), checkout)
    if _canonical_repository(origin) != _canonical_repository(dependency.repository):
        raise DependencyError(
            f"{checkout} has origin {origin}, expected {dependency.repository}; refusing to use it"
        )
    dirty = _run_git(("status", "--porcelain", "--untracked-files=all"), checkout)
    if dirty:
        raise DependencyError(f"{checkout} has local changes; refusing a mutable runtime input")
    measured = _run_git(("rev-parse", "HEAD"), checkout)
    if measured != dependency.revision:
        raise DependencyError(
            f"{checkout} is at {measured}, but LF2 requires {dependency.revision}"
        )
    if not (checkout / dependency.marker).is_file():
        raise DependencyError(
            f"{checkout} is pinned but missing required marker {dependency.marker}"
        )
    uninitialized = [
        line.split()[1]
        for line in _run_git(("submodule", "status", "--recursive"), checkout).splitlines()
        if line.startswith("-")
    ]
    if uninitialized:
        raise DependencyError(
            f"{checkout} is pinned but has uninitialized submodules: {', '.join(uninitialized)}"
        )
    return checkout


def _scoped_clean(root: Path, target: Path) -> None:
    dependency_root = (root / "build" / "deps").resolve()
    target = target.resolve()
    if target == dependency_root or dependency_root not in target.parents:
        raise DependencyError(
            f"refusing to replace {target}; provisioned dependencies must be children of "
            f"{dependency_root}"
        )
    if target.exists():
        shutil.rmtree(target)


def provision_checkout(
    root: Path, dependency: SourceDependency, *, replace: bool = False
) -> Path:
    root = root.resolve()
    target = root / "build" / "deps" / dependency.name
    if target.exists() and not replace:
        return validate_checkout(dependency, target)
    if replace:
        _scoped_clean(root, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{dependency.name}-", dir=target.parent) as raw:
        staged = Path(raw) / dependency.name
        _run_git(("clone", "--no-checkout", dependency.repository, str(staged)))
        _run_git(("checkout", "--detach", dependency.revision), staged)
        _run_git(("submodule", "update", "--init", "--recursive"), staged)
        validate_checkout(dependency, staged)
        if target.exists():
            raise DependencyError(f"{target} appeared during provisioning; refusing to overwrite it")
        staged.replace(target)
    return validate_checkout(dependency, target)


def resolve_checkout(
    root: Path,
    dependency: SourceDependency,
    environment: Mapping[str, str] = os.environ,
) -> Path:
    """Resolve an exact checkout or provision a build-local one.

    Explicit configuration and a configured shared root are authoritative: a
    missing or wrong checkout there is refused rather than silently bypassed.
    The workspace sibling is accepted only when it is the exact clean pin.
    """

    root = root.resolve()
    explicit = environment.get(dependency.environment_key)
    if explicit:
        target = Path(explicit).expanduser()
        if not target.is_absolute():
            target = root / target
        if not target.exists():
            raise DependencyError(
                f"{dependency.environment_key}={target} does not exist; expected {dependency.marker}"
            )
        return validate_checkout(dependency, target)

    shared_root = environment.get("SHARED_DIR")
    if shared_root:
        target = Path(shared_root).expanduser()
        if not target.is_absolute():
            target = root / target
        target /= dependency.name
        if not target.exists():
            raise DependencyError(
                f"SHARED_DIR resolves {dependency.name} to missing {target}; expected "
                f"{dependency.marker}"
            )
        return validate_checkout(dependency, target)

    sibling = root.parent.parent / "shared" / dependency.name
    if sibling.exists():
        return validate_checkout(dependency, sibling)
    return provision_checkout(root, dependency)


def resolve_runtime_dependencies(
    root: Path,
    environment: Mapping[str, str] = os.environ,
    *,
    refresh: bool = False,
) -> dict[str, Path]:
    if refresh:
        return {
            dependency.name: provision_checkout(root, dependency, replace=True)
            for dependency in RUNTIME_DEPENDENCIES
        }
    return {
        dependency.name: resolve_checkout(root, dependency, environment)
        for dependency in RUNTIME_DEPENDENCIES
    }
