#!/usr/bin/env python3
"""Positive and controlled-negative checks for the shipping execution policy."""

from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))

from execution_boundary import REQUIRED_FILES, inspect  # noqa: E402
from product_symbols import inspect_nm_output  # noqa: E402


class ExecutionBoundaryTest(unittest.TestCase):
    def test_repository_satisfies_policy(self) -> None:
        inspected, violations = inspect(ROOT)
        self.assertGreaterEqual(inspected, len(REQUIRED_FILES))
        self.assertEqual([], violations)

    def test_missing_jit_adapter_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in REQUIRED_FILES:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "set(LF2_JIT_EXECUTOR runtime/cpu/jit_executor.c)\n"
                "if(TARGET x86port_runtime)\nendif()\n"
                "target_link_libraries(lf2 PRIVATE x86port_runtime)\n",
                encoding="utf-8",
            )
            (root / "runtime" / "cpu" / "jit_executor.c").unlink()

            _, violations = inspect(root)

        self.assertTrue(
            any(
                violation.path == "runtime/cpu/jit_executor.c" and "missing" in violation.detail
                for violation in violations
            ),
            violations,
        )

    def test_test_oracle_symbol_is_rejected(self) -> None:
        audit = inspect_nm_output(
            "00000000 T lf2_jit_call\n"
            "00000010 T lf2_jit_call_original\n"
            "00000020 T x86p_jit_engine_run\n"
            "00000030 T x86port_test_oracle\n"
        )
        self.assertEqual(("x86port_test_oracle",), audit.forbidden)
        self.assertEqual((), audit.missing)


if __name__ == "__main__":
    unittest.main()
