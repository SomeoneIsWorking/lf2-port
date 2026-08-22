#!/usr/bin/env python3
"""The embedded overlay subset must map every runtime CJK label to a real outline."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/build"))
from subset_overlay_font import required_characters  # noqa: E402


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def format4_mappings(data: bytes, table: int) -> dict[int, int]:
    segment_count = u16(data, table + 6) // 2
    end_codes = table + 14
    start_codes = end_codes + segment_count * 2 + 2
    deltas = start_codes + segment_count * 2
    range_offsets = deltas + segment_count * 2
    result: dict[int, int] = {}
    for index in range(segment_count):
        start = u16(data, start_codes + index * 2)
        end = u16(data, end_codes + index * 2)
        if end == 0xFFFF:
            continue
        delta = u16(data, deltas + index * 2)
        range_offset_word = range_offsets + index * 2
        range_offset = u16(data, range_offset_word)
        for codepoint in range(start, end + 1):
            if range_offset == 0:
                glyph = (codepoint + delta) & 0xFFFF
            else:
                glyph = u16(data, range_offset_word + range_offset + 2 * (codepoint - start))
                if glyph:
                    glyph = (glyph + delta) & 0xFFFF
            if glyph:
                result[codepoint] = glyph
    return result


def table_offsets(data: bytes) -> dict[bytes, tuple[int, int]]:
    result: dict[bytes, tuple[int, int]] = {}
    for index in range(u16(data, 4)):
        entry = 12 + index * 16
        result[data[entry : entry + 4]] = (u32(data, entry + 8), u32(data, entry + 12))
    return result


def glyph_offsets(data: bytes, tables: dict[bytes, tuple[int, int]]) -> list[int]:
    head = tables[b"head"][0]
    loca = tables[b"loca"][0]
    glyph_count = u16(data, tables[b"maxp"][0] + 4)
    if u16(data, head + 50) != 0:
        return [u32(data, loca + 4 * index) for index in range(glyph_count + 1)]
    return [2 * u16(data, loca + 2 * index) for index in range(glyph_count + 1)]


def mapped_glyphs(data: bytes, tables: dict[bytes, tuple[int, int]]) -> dict[int, int]:
    cmap = tables[b"cmap"][0]
    result: dict[int, int] = {}
    for index in range(u16(data, cmap + 2)):
        record = cmap + 4 + index * 8
        subtable = cmap + u32(data, record + 4)
        if u16(data, subtable) == 4:
            result.update(format4_mappings(data, subtable))
    return result


def names(data: bytes, tables: dict[bytes, tuple[int, int]]) -> dict[int, set[str]]:
    table = tables[b"name"][0]
    strings = table + u16(data, table + 4)
    result: dict[int, set[str]] = {}
    for index in range(u16(data, table + 2)):
        record = table + 6 + index * 12
        platform = u16(data, record)
        name_id = u16(data, record + 6)
        length = u16(data, record + 8)
        offset = strings + u16(data, record + 10)
        encoding = "utf-16-be" if platform in (0, 3) else "mac-roman"
        result.setdefault(name_id, set()).add(data[offset : offset + length].decode(encoding))
    return result


def main() -> int:
    font = Path(sys.argv[1])
    label_source = Path(sys.argv[2])
    data = font.read_bytes()
    tables = table_offsets(data)
    mappings = mapped_glyphs(data, tables)
    offsets = glyph_offsets(data, tables)
    required_codes = {ord(character) for character in required_characters(label_source)}
    mapped_codes = set(mappings)
    missing = sorted(required_codes - mapped_codes)
    extras = sorted(mapped_codes - required_codes)
    empty = sorted(
        codepoint
        for codepoint, glyph in mappings.items()
        if glyph >= len(offsets) - 1 or offsets[glyph] == offsets[glyph + 1]
    )
    source_notices = {
        7: "Droid is a trademark of Google and may be registered in certain jurisdictions.",
        8: "Ascender Corporation",
        9: "Steve Matteson",
        10: "Droid Sans is a humanist sans serif typeface designed for user interfaces and electronic communications.",
        11: "http://www.ascendercorp.com/",
        12: "http://www.ascendercorp.com/typedesigners.html",
        13: "Licensed under the Apache License, Version 2.0",
        14: "http://www.apache.org/licenses/LICENSE-2.0",
    }
    embedded_names = names(data, tables)
    altered_notices = sorted(
        name_id for name_id, notice in source_notices.items() if notice not in embedded_names.get(name_id, set())
    )
    if missing or extras or empty or altered_notices:
        print(
            f"FAIL overlay CJK subset missing={missing} extras={extras} empty={empty} "
            f"altered_notice_name_ids={altered_notices}",
            file=sys.stderr,
        )
        return 1
    print(
        f"overlay CJK subset: {len(required_codes)} mapped runtime glyphs, all with outlines, "
        "no extras, source notice name IDs 7-14 retained"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
