#!/usr/bin/env python3
"""Decode LF2's timestamped Lucent records for diagnostic analyzers."""

from __future__ import annotations

import re


PREFIX = re.compile(
    r"^\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z\] \[[^\]\n]+\] "
)


def is_record(line: str) -> bool:
    return PREFIX.match(line) is not None


def payload_line(line: str) -> str:
    match = PREFIX.match(line)
    return line[match.end() :] if match else line


def payload_text(text: str) -> str:
    return "".join(payload_line(line) for line in text.splitlines(keepends=True))
