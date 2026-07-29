#!/usr/bin/env python3
"""Unpack the LF2 self-extracting installer overlay.

Container format (little-endian), starting at the PE overlay:

    magic   'wwgT'                4 bytes
    id                            4 bytes
    u16                           2 bytes
    record*                       until EOF

    record: comp_size   u32       size of (method byte + stream + trailer)
            uncomp_size u32
            method      u8        1 = zlib, 2 = bzip2, 0 = stored
            stream      bytes
            trailer     4 bytes   (checksum)

Usage: unpack_installer.py <installer.exe> <outdir>
"""

import bz2
import os
import struct
import sys
import zlib

MAGIC = b"wwgT"


def overlay_offset(data: bytes) -> int:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    sect = pe + 24 + optsz
    end = 0
    for i in range(nsec):
        raw_size, raw_ptr = struct.unpack_from("<II", data, sect + i * 40 + 16)
        end = max(end, raw_ptr + raw_size)
    return end


def decompress(method: int, stream: bytes, uncomp_size: int) -> bytes:
    if method == 1:
        return zlib.decompress(stream)
    if method == 2:
        return bz2.decompress(stream)
    if method == 0:
        return stream[:uncomp_size]
    raise ValueError(f"unknown compression method {method}")


def unpack(path: str, outdir: str) -> list:
    data = open(path, "rb").read()
    pos = overlay_offset(data)
    if data[pos:pos + 4] != MAGIC:
        raise SystemExit(f"no {MAGIC!r} magic at overlay offset {pos:#x}")

    os.makedirs(outdir, exist_ok=True)
    pos += 10  # magic + id + u16
    records = []
    while pos + 8 <= len(data):
        comp_size, uncomp_size = struct.unpack_from("<II", data, pos)
        if comp_size < 5 or pos + 8 + comp_size > len(data):
            break
        method = data[pos + 8]
        stream = data[pos + 9:pos + 8 + comp_size - 4]
        blob = decompress(method, stream, uncomp_size)
        name = f"{len(records):03d}.bin"
        open(os.path.join(outdir, name), "wb").write(blob)
        records.append((name, pos, comp_size, uncomp_size, len(blob), method))
        pos += 8 + comp_size
    return records


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    for name, off, c, u, got, m in unpack(sys.argv[1], sys.argv[2]):
        flag = "" if got == u else f"  !! declared {u}"
        print(f"{name}  off={off:#x} comp={c} unc={got} method={m}{flag}")


if __name__ == "__main__":
    main()
