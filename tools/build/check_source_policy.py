#!/usr/bin/env python3
"""Enforce LF2's repository-wide source and tooling ownership policy."""

from pathlib import Path

from source_policy import inspect_source_policy


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    inspected, violations = inspect_source_policy(ROOT)
    if violations:
        print(f"source-policy: inspected {inspected} first-party files; {len(violations)} violation(s)")
        for violation in violations:
            print(f"  {violation.path}:{violation.line}: {violation.detail}")
        return 1
    print(f"source-policy: ok; inspected {inspected} first-party files; 0 policy violations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
