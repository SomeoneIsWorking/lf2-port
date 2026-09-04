#!/usr/bin/env python3
"""Positive and controlled-negative tests for product source ownership."""

from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))

from source_policy import inspect_source_policy  # noqa: E402


class SourcePolicyTest(unittest.TestCase):
    def test_repository_satisfies_policy(self) -> None:
        inspected, violations = inspect_source_policy(ROOT)
        self.assertGreater(inspected, 1)
        self.assertEqual([], violations)

    def test_direct_environment_read_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "runtime" / "video" / "bad.c"
            source.parent.mkdir(parents=True)
            source.write_text('const char *bad(void) { return getenv("LF2_BAD"); }\n', encoding="utf-8")
            _, violations = inspect_source_policy(root)
        self.assertEqual("environment read bypasses typed owner", violations[0].detail)

    def test_direct_stderr_write_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "runtime" / "video" / "bad.c"
            source.parent.mkdir(parents=True)
            source.write_text('void bad(void) { fprintf(stderr, "hidden"); }\n', encoding="utf-8")
            _, violations = inspect_source_policy(root)
        self.assertTrue(any("logger" in violation.detail for violation in violations), violations)

    def test_retired_execution_interface_is_rejected_in_documentation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = root / "docs" / "old.md"
            document.parent.mkdir(parents=True)
            document.write_text("Restore " + "lf2_" + "recomp.c.\n", encoding="utf-8")
            _, violations = inspect_source_policy(root)
        self.assertTrue(any("retired execution interface" in violation.detail for violation in violations), violations)

    def test_non_launcher_shell_tool_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "tools" / "bad.sh"
            tool.parent.mkdir(parents=True)
            tool.write_text("#!/bin/sh\n", encoding="utf-8")
            _, violations = inspect_source_policy(root)
        self.assertTrue(any("automation must be Python" in violation.detail for violation in violations), violations)

    def test_java_tool_is_rejected_but_android_source_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "tools" / "re" / "Bad.java"
            tool.parent.mkdir(parents=True)
            tool.write_text("final class Bad {}\n", encoding="utf-8")
            activity = root / "platforms" / "android" / "MainActivity.java"
            activity.parent.mkdir(parents=True)
            activity.write_text("final class MainActivity {}\n", encoding="utf-8")
            _, violations = inspect_source_policy(root)
        self.assertEqual(1, len(violations), violations)
        self.assertEqual("tool automation must be Python/Jython", violations[0].detail)


if __name__ == "__main__":
    unittest.main()
