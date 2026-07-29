#!/usr/bin/env python3
"""Extract the LF2 game tree straight out of the installer, no Windows needed.

The installer is a Win32 PE stub with a custom overlay:

    'wwgT' + id + u16
    script records:  comp_size u32, uncomp_size u32, method u8, stream
                     (comp_size covers the method byte, the stream and a
                      4-byte trailer)
    file payload:    method u8, stream          -- repeated, back to back,
                                                   no lengths, no trailer

Script record 5 is the file table: length-prefixed entries, each holding the
compressed size (+10), the uncompressed size (+18) and a NUL-terminated
destination path (+62). Payload blobs appear in file-table order.

Usage: extract_game.py <installer.exe> <outdir>
"""

import bz2
import os
import struct
import sys
import zlib

from unpack_installer import overlay_offset, unpack

TABLE_RECORD = 5
FIRST_ENTRY = 133
NAME_OFFSET = 62


def parse_table(table: bytes) -> list:
    """Yield (path, comp_size, uncomp_size) in payload order."""
    entries = []
    pos = FIRST_ENTRY
    while pos + 8 <= len(table):
        length = struct.unpack_from("<I", table, pos)[0]
        if length < NAME_OFFSET or pos + length > len(table):
            break
        rec = table[pos:pos + length]
        comp, uncomp = struct.unpack_from("<I", rec, 10)[0], struct.unpack_from("<I", rec, 18)[0]
        name = rec[NAME_OFFSET:].split(b"\0")[0].decode("latin1")
        entries.append((name, comp, uncomp))
        pos += length
    return entries


def payload_start(data: bytes) -> int:
    """Offset just past the last script record."""
    pos = overlay_offset(data) + 10
    while True:
        comp_size = struct.unpack_from("<I", data, pos)[0]
        method = data[pos + 8]
        body = data[pos + 9:]
        dec = zlib.decompressobj() if method == 1 else bz2.BZ2Decompressor()
        dec.decompress(body)
        used = len(body) - len(dec.unused_data)
        if pos + 8 + comp_size >= len(data):
            return pos + 9 + used  # last record: comp_size runs to EOF
        pos += 8 + comp_size


def blobs(data: bytes, start: int):
    pos = start
    while pos < len(data) - 1:
        method = data[pos]
        if method not in (1, 2):
            raise ValueError(f"bad method {method} at {pos:#x}")
        dec = zlib.decompressobj() if method == 1 else bz2.BZ2Decompressor()
        out = dec.decompress(data[pos + 1:])
        used = len(data) - (pos + 1) - len(dec.unused_data)
        yield pos, 1 + used, out
        pos += 1 + used


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    installer, outdir = sys.argv[1], sys.argv[2]
    data = open(installer, "rb").read()

    scripts = os.path.join(outdir, ".installer")
    unpack(installer, scripts)
    table = open(os.path.join(scripts, f"{TABLE_RECORD:03d}.bin"), "rb").read()
    entries = parse_table(table)

    # The installer stores each distinct file once; a table entry whose sizes
    # don't match the next unconsumed blob is a duplicate of an earlier entry.
    stream = blobs(data, payload_start(data))
    pending = next(stream, None)
    by_size = {}
    written = duplicates = 0

    for name, comp, uncomp in entries:
        if pending and pending[1] == comp and len(pending[2]) == uncomp:
            content = pending[2]
            by_size[(comp, uncomp)] = content
            pending = next(stream, None)
        elif (comp, uncomp) in by_size:
            content = by_size[(comp, uncomp)]
            duplicates += 1
        else:
            raise SystemExit(f"unresolved entry {name} ({comp}/{uncomp})")

        dest = os.path.join(outdir, *name.split("\\"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(content)
        written += 1

    if pending is not None:
        raise SystemExit("payload blobs left over after the file table ran out")
    print(f"{written} files written to {outdir} ({duplicates} deduplicated)")


if __name__ == "__main__":
    main()
