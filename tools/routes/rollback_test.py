#!/usr/bin/env python3
"""Roll every frame back once and require the same timeline as a run that never rolls back.

With LF2_ROLLBACK_CHECK each frame runs unshown, is loaded back from its snapshot and runs again
shown; the two runs of a frame must end in the same state, and every frame's logic and media
digests must equal those of a plain run with the same inputs.
"""

from __future__ import annotations

import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from determinism_test import FRAMES, first_difference, run_once


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("BUILD") or root / "build" / "clang").resolve()
    game = Path(os.environ.get("GAME") or root / "game").resolve()
    out = root / "scratch" / "rollback"
    out.mkdir(parents=True, exist_ok=True)

    print(f"a plain run and a rolled-back run of {FRAMES} frames with the smoke route's inputs...")
    plain_exit, plain = run_once(build, game, out / "plain.log")
    checked_exit, checked = run_once(build, game, out / "checked.log", {"LF2_ROLLBACK_CHECK": "1"})
    if plain_exit or checked_exit:
        print(f"  FAIL  a run did not quit at frame {FRAMES} (exit {plain_exit}, {checked_exit}); see {out}")
        return 1
    log = (out / "checked.log").read_text(errors="replace")
    reruns = log.count("[rollback-check] ")
    differing = log.count("differs on rerun")
    print(f"  {'ok  ' if reruns and not differing else 'FAIL'}  every frame reran to the same state "
          f"({differing} differed; see {out / 'checked.log'})")
    logic = first_difference(plain, checked, 1)
    media = first_difference(plain, checked, 2)
    same_length = bool(plain) and len(plain) == len(checked)
    print(f"  {'ok  ' if same_length else 'FAIL'}  frames compared: {min(len(plain), len(checked))} "
          f"(plain={len(plain)}, rolled back={len(checked)})")
    print(f"  {'ok  ' if logic is None else 'FAIL'}  logic: "
          f"{'identical' if logic is None else f'first differs at frame {logic}'}")
    print(f"  {'ok  ' if media is None else 'FAIL'}  media: "
          f"{'identical' if media is None else f'first differs at frame {media}'}")
    passed = reruns and not differing and same_length and logic is None and media is None
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
