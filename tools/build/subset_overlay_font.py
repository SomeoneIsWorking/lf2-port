#!/usr/bin/env python3
"""Build the licensed CJK subset used by the native pre-fight panel."""

from __future__ import annotations

import argparse
import ast
import hashlib
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LABEL_SOURCE = ROOT / "runtime/ui/overlay_panel.c"
CJK_RUN = re.compile(r'\{"(?:\\.|[^"\\])*", "((?:\\.|[^"\\])*)",')


def required_characters(source: Path = LABEL_SOURCE) -> set[str]:
    runs = CJK_RUN.findall(source.read_text(encoding="utf-8"))
    if len(runs) != 12:
        raise ValueError(f"expected 12 CJK label runs in {source}, found {len(runs)}")
    decoded = [ast.literal_eval(f'"{run}"').encode("latin-1").decode("utf-8") for run in runs]
    return set("".join(decoded))


def set_subset_names(font: object) -> None:
    names = font["name"]
    replacements = {
        1: "Droid Sans Fallback LF2 Overlay Subset",
        3: "Droid Sans Fallback LF2 Overlay Subset; 2026-08-22",
        4: "Droid Sans Fallback LF2 Overlay Subset",
        5: "Version 1.00c; modified LF2 overlay subset 2026-08-22",
        6: "DroidSansFallback-LF2OverlaySubset",
    }
    for name_id, value in replacements.items():
        names.setName(value, name_id, 3, 1, 0x0409)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    from fontTools import subset
    from fontTools.ttLib import TTFont

    font = TTFont(args.source, recalcTimestamp=False)
    options = subset.Options()
    options.name_IDs = list(range(15))
    worker = subset.Subsetter(options=options)
    worker.populate(unicodes={ord(character) for character in required_characters()})
    worker.subset(font)
    set_subset_names(font)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output, reorderTables=True)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(f"wrote {args.output} ({args.output.stat().st_size} bytes, sha256 {digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
