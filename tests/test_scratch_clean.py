#!/usr/bin/env python3
"""Scoped scratch cleanup preserves its directory and refuses broad targets."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build.scratch_clean import (  # noqa: E402
    ScratchCleanError,
    empty_scratch_child,
    resolve_scratch_child,
)


class ScratchCleanTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "scratch")
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_named_child_is_emptied_but_preserved(self) -> None:
        target = self.root / "scratch" / "captures"
        nested = target / "nested"
        nested.mkdir(parents=True)
        (target / "frame.ppm").write_bytes(b"frame")
        (nested / "run.log").write_text("run\n", encoding="utf-8")
        resolved, removed = empty_scratch_child(self.root, "captures")
        self.assertEqual(resolved, target)
        self.assertEqual(removed, 3)
        self.assertTrue(target.is_dir())
        self.assertEqual([], list(target.iterdir()))

    def test_broad_and_escaping_paths_are_refused(self) -> None:
        for requested in ("scratch", "../docs", "/tmp/lf2"):
            with self.subTest(requested=requested):
                with self.assertRaises(ScratchCleanError):
                    resolve_scratch_child(self.root, requested)


if __name__ == "__main__":
    unittest.main()
