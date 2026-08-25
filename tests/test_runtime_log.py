#!/usr/bin/env python3
"""Offline checks for the shared Lucent record decoder."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from runtime_log import is_record, payload_line, payload_text


def main() -> int:
    first = "[2026-08-25T15:42:17.083Z] [port_entry] startup ready\n"
    second = "[2026-08-25T15:42:18.004Z] [com:warn] partial row\n"
    malformed = "[2026-08-25 15:42:18] [com] not a Lucent record\n"

    assert is_record(first)
    assert is_record(second)
    assert not is_record(malformed)
    assert payload_line(first) == "startup ready\n"
    assert payload_text(first + malformed + second) == (
        "startup ready\n" + malformed + "partial row\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
