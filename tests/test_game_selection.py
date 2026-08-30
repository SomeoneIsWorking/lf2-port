#!/usr/bin/env python3
"""Exercise direct, ZIP, and original-installer setup selections and their refusal paths."""

from __future__ import annotations

import bz2
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path


def run(
    binary: Path,
    selection: Path,
    config: Path,
    expected_success: bool,
    *,
    staged: bool = False,
) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ)
    environment["XDG_CONFIG_HOME"] = str(config)
    result = subprocess.run(
        [str(binary), *(["--staged"] if staged else []), str(selection)],
        text=True,
        capture_output=True,
        env=environment,
        check=False,
    )
    if (result.returncode == 0) != expected_success:
        raise AssertionError(
            f"selection {selection} returned {result.returncode}\nstdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def extracted_files(root: Path) -> dict[Path, bytes]:
    return {
        path.relative_to(root): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file()
    }


def compressed_blob(content: bytes, method: int) -> bytes:
    if method == 1:
        stream = zlib.compress(content)
    elif method == 2:
        stream = bz2.compress(content)
    else:
        raise AssertionError(f"unsupported fixture method {method}")
    return bytes([method]) + stream


def table_entry(name: str, blob: bytes, content: bytes) -> bytes:
    encoded = name.encode("ascii") + b"\0"
    record = bytearray(62 + len(encoded))
    struct.pack_into("<I", record, 0, len(record))
    struct.pack_into("<I", record, 10, len(blob))
    struct.pack_into("<I", record, 18, len(content))
    record[62:] = encoded
    return bytes(record)


def script_record(content: bytes, method: int) -> bytes:
    blob = compressed_blob(content, method)
    return struct.pack("<II", len(blob) + 4, len(content)) + blob + b"\0\0\0\0"


def make_installer(path: Path, files: list[tuple[str, bytes, int, bool]]) -> None:
    table = bytearray(133)
    payload = bytearray()
    for name, content, method, duplicate in files:
        blob = compressed_blob(content, method)
        table.extend(table_entry(name, blob, content))
        if not duplicate:
            payload.extend(blob)

    stub = bytearray(0x200)
    stub[0:2] = b"MZ"
    struct.pack_into("<I", stub, 0x3C, 0x80)
    stub[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", stub, 0x86, 1)
    struct.pack_into("<H", stub, 0x94, 0)
    struct.pack_into("<I", stub, 0x98 + 16, 0x100)
    struct.pack_into("<I", stub, 0x98 + 20, 0x100)

    scripts = b"".join(script_record(f"script-{index}".encode(), 1 + index % 2) for index in range(5))
    scripts += script_record(bytes(table), 1)
    marker_content = b"LF2 payload follows"
    marker = compressed_blob(marker_content, 2)
    marker_span = len(marker) + len(payload)
    marker_header = struct.pack("<II", marker_span, marker_span)
    path.write_bytes(bytes(stub) + b"wwgT" + b"\0" * 6 + scripts + marker_header + marker + payload)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_game_selection.py <binary> <scratch-root>")
    binary = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve() / "game-selection"
    if root.exists():
        shutil.rmtree(root)
    source = root / "source"
    config = root / "config"
    source.mkdir(parents=True)

    executable = source / "lf2.exe"
    executable.write_bytes(b"fixture")
    data = source / "data"
    data.mkdir()
    (data / "data.txt").write_text("file: data/fixture.dat\n", encoding="ascii")
    (data / "fixture.dat").write_bytes(b"asset")
    direct = run(binary, executable, config, True)
    assert Path(direct.stdout.strip()) == executable
    directory = run(binary, source, config, True)
    assert Path(directory.stdout.strip()) == executable

    nested = root / "nested.zip"
    with zipfile.ZipFile(nested, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Install/LF2/lf2.exe", b"fixture")
        archive.writestr("Install/LF2/data/data.txt", b"file: data/fixture.dat\n")
        archive.writestr("Install/LF2/data/fixture.dat", b"asset")
    nested_result = run(binary, nested, config, True)
    assert Path(nested_result.stdout.strip()).name.lower() == "lf2.exe"
    assert Path(nested_result.stdout.strip()).is_file()
    repeated_result = run(binary, nested, config, True)
    assert Path(repeated_result.stdout.strip()) == Path(nested_result.stdout.strip())
    assert not list(config.rglob("*.preparing"))
    assert not list(config.rglob("*.previous"))

    accepted = Path(nested_result.stdout.strip())
    with zipfile.ZipFile(nested, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Broken/LF2/lf2.exe", b"fixture")
        archive.writestr("Broken/LF2/data/data.txt", b"file: data/missing.dat\n")
    assert "missing game data" in run(binary, nested, config, False).stderr
    assert accepted.read_bytes() == b"fixture"
    assert not list(config.rglob("*.preparing"))
    assert not list(config.rglob("*.previous"))

    duplicate = root / "duplicate.zip"
    with zipfile.ZipFile(duplicate, "w") as archive:
        archive.writestr("one/lf2.exe", b"one")
        archive.writestr("two/LF2.EXE", b"two")
    assert "more than one" in run(binary, duplicate, config, False).stderr

    traversal = root / "traversal.zip"
    with zipfile.ZipFile(traversal, "w") as archive:
        archive.writestr("../lf2.exe", b"fixture")
    assert "unsafe path" in run(binary, traversal, config, False).stderr

    oversized = root / "oversized.zip"
    with oversized.open("wb") as archive:
        archive.seek(512 * 1024 * 1024)
        archive.write(b"\0")
    assert "compressed byte limit" in run(binary, oversized, config, False).stderr

    installer_manifest = b"file: data/fixture.dat\nfile: data/copy.dat\n"
    installer_files = [
        ("lf2.exe", b"fixture", 1, False),
        ("data\\data.txt", installer_manifest, 2, False),
        ("data\\fixture.dat", b"asset", 2, False),
        ("data\\copy.dat", b"asset", 2, True),
    ]
    installer = root / "LF2_v2.0a.exe"
    make_installer(installer, installer_files)
    installer_result = run(binary, installer, config, True)
    installed_executable = Path(installer_result.stdout.strip())
    assert installed_executable.read_bytes() == b"fixture"
    assert (installed_executable.parent / "data" / "copy.dat").read_bytes() == b"asset"

    python_extract = root / "python-extract"
    python_temp = root / "python-temp"
    python_extract.mkdir()
    python_temp.mkdir()
    python_environment = dict(os.environ)
    python_environment["TMPDIR"] = str(python_temp)
    subprocess.run(
        [
            sys.executable,
            str(Path(__file__).resolve().parents[1] / "tools" / "extract_game.py"),
            str(installer),
            str(python_extract),
        ],
        env=python_environment,
        check=True,
        capture_output=True,
        text=True,
    )
    assert extracted_files(python_extract) == extracted_files(installed_executable.parent)

    staged_root = root / "android-staging"
    staged_root.mkdir()
    staged_installer = staged_root / "installer.exe"
    shutil.copy2(installer, staged_installer)
    staged_result = run(binary, staged_installer, config, True, staged=True)
    staged_executable = Path(staged_result.stdout.strip())
    assert staged_executable.parent == staged_root / "prepared"
    assert staged_executable.read_bytes() == b"fixture"

    staged_zip_root = root / "android-zip-staging"
    staged_zip_root.mkdir()
    staged_zip = staged_zip_root / "game.zip"
    with zipfile.ZipFile(staged_zip, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Install/LF2/lf2.exe", b"fixture")
        archive.writestr("Install/LF2/data/data.txt", b"file: data/fixture.dat\n")
        archive.writestr("Install/LF2/data/fixture.dat", b"asset")
    staged_zip_result = run(binary, staged_zip, config, True, staged=True)
    staged_zip_executable = Path(staged_zip_result.stdout.strip())
    assert staged_zip_executable.parent == staged_zip_root / "prepared" / "Install" / "LF2"
    assert staged_zip_executable.read_bytes() == b"fixture"

    unsafe_installer = root / "unsafe-installer.exe"
    make_installer(unsafe_installer, [("../lf2.exe", b"fixture", 1, False)])
    assert "unsafe file path" in run(binary, unsafe_installer, config, False).stderr
    assert installed_executable.read_bytes() == b"fixture"

    truncated_installer = root / "truncated-installer.exe"
    truncated_installer.write_bytes(installer.read_bytes()[:-1])
    assert "could not be used" in run(binary, truncated_installer, config, False).stderr
    assert installed_executable.read_bytes() == b"fixture"

    print(
        "game selection: direct, ZIP, and zlib/bzip2 installer imports accepted; native/Python extraction "
        "matched; invalid replacements preserved; duplicate, traversal, truncation, and budget checks passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
