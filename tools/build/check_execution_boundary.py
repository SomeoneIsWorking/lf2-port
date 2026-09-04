#!/usr/bin/env python3
"""Fail when LF2 loses its JIT-default execution boundary."""

from pathlib import Path
import sys

from execution_boundary import inspect


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    inspected, violations = inspect(ROOT)
    if violations:
        print(f"execution-boundary: FAILED after inspecting {inspected} files")
        for violation in violations:
            print(f"  {violation.path}: {violation.detail}")
        return 1
    print(
        f"execution-boundary: ok; inspected {inspected} files; "
        "gameplay has one explicit JIT adapter boundary and no explicit interpreter selector"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
