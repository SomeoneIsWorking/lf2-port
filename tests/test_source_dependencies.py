#!/usr/bin/env python3
"""Pinned shared-runtime resolution accepts only exact, immutable checkouts."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))

from source_dependencies import (  # noqa: E402
    DependencyError,
    SourceDependency,
    X86PORT,
    resolve_checkout,
)


def git(arguments: list[str], cwd: Path) -> str:
    return subprocess.run(
        ["git", *arguments], cwd=cwd, check=True, text=True, capture_output=True
    ).stdout.strip()


class SourceDependencyTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "scratch")
        self.workspace = Path(self.temporary.name)
        self.upstream = self.workspace / "upstream"
        self.upstream.mkdir()
        git(["init", "--quiet"], self.upstream)
        git(["config", "user.name", "LF2 test"], self.upstream)
        git(["config", "user.email", "lf2-test@example.invalid"], self.upstream)
        marker = self.upstream / "runtime.marker"
        marker.write_text("runtime\n", encoding="utf-8")
        git(["add", "runtime.marker"], self.upstream)
        git(["commit", "--quiet", "-m", "runtime fixture"], self.upstream)
        self.revision = git(["rev-parse", "HEAD"], self.upstream)
        self.dependency = SourceDependency(
            name="fixture-runtime",
            repository=str(self.upstream),
            revision=self.revision,
            marker="runtime.marker",
        )
        self.root = self.workspace / "pc" / "lf2"
        self.root.mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_x86port_pin_is_the_landed_runtime_revision(self) -> None:
        self.assertEqual(X86PORT.revision, "9b224ebdb2bfc9e60fa507cdf98b3ab389f3f814")

    def test_missing_checkout_is_provisioned_at_exact_revision(self) -> None:
        resolved = resolve_checkout(self.root, self.dependency, {})
        self.assertEqual(resolved, self.root / "build" / "deps" / self.dependency.name)
        self.assertEqual(git(["rev-parse", "HEAD"], resolved), self.revision)
        self.assertEqual(resolve_checkout(self.root, self.dependency, {}), resolved)

    def test_dirty_explicit_checkout_is_refused(self) -> None:
        resolved = resolve_checkout(self.root, self.dependency, {})
        (resolved / "runtime.marker").write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(DependencyError, "local changes"):
            resolve_checkout(
                self.root,
                self.dependency,
                {self.dependency.environment_key: str(resolved)},
            )

    def test_wrong_explicit_revision_is_refused(self) -> None:
        resolved = resolve_checkout(self.root, self.dependency, {})
        (resolved / "second").write_text("second\n", encoding="utf-8")
        git(["add", "second"], resolved)
        git(["config", "user.name", "LF2 test"], resolved)
        git(["config", "user.email", "lf2-test@example.invalid"], resolved)
        git(["commit", "--quiet", "-m", "wrong revision"], resolved)
        with self.assertRaisesRegex(DependencyError, "LF2 requires"):
            resolve_checkout(
                self.root,
                self.dependency,
                {self.dependency.environment_key: str(resolved)},
            )


if __name__ == "__main__":
    unittest.main()
