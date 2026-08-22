"""Authoritative reader for the binary PPM frame dumps used by route tests."""

from __future__ import annotations

from pathlib import Path
from typing import BinaryIO


def _token(stream: BinaryIO, path: Path) -> bytes:
    while True:
        byte = stream.read(1)
        if not byte:
            raise ValueError(f"{path}: unexpected end of PPM header")
        if byte == b"#":
            stream.readline()
            continue
        if not byte.isspace():
            break
    token = bytearray(byte)
    while True:
        byte = stream.read(1)
        if not byte or byte.isspace():
            return bytes(token)
        token.extend(byte)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Return width, height and packed RGB bytes from an 8-bit binary PPM."""
    with path.open("rb") as stream:
        if _token(stream, path) != b"P6":
            raise ValueError(f"{path} is not a binary PPM")
        width = int(_token(stream, path))
        height = int(_token(stream, path))
        if int(_token(stream, path)) != 255:
            raise ValueError(f"{path} is not an 8-bit RGB PPM")
        pixels = stream.read()
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"{path} has {len(pixels)} pixel bytes, expected {expected}")
    return width, height, pixels
