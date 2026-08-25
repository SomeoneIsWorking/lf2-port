#!/usr/bin/env python3
"""Run the game-booting end-to-end routes serially.

These routes answer questions that require a running game. They intentionally remain outside
ctest, whose offline suite is kept fast enough to run after every edit.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROUTES = (
    ("smoke", "the port boots, loads its data and reaches a match"),
    ("settings", "mapped controller navigation reaches RmlUi Controls and both shared SVG device icons"),
    ("ui_escape", "physical SDL keyboard and pointer control RmlUi; no scripted-key substitute"),
    ("ui_global", "the same RmlUi shell opens on menu, selection, overlay, and match screens"),
    ("cheats", "RmlUi activates the built-in F6 cheat and the guest consumes it"),
    ("object_parser", "the native object parser is byte-identical to LF2's original parser"),
    ("controller", "a pad alone drives the game -- the mouse and key routes cannot see this path"),
    ("mouse", "the pointer alone reaches charselect, the overlay and a match (issue #26)"),
    ("controller_2p", "a second pad joins as player two, with a one-pad run as the control"),
    ("coop_dropin", "a pad joins a match ALREADY RUNNING and drives the fighter it joined"),
    ("coop_select", "a late joiner picks its character from the game's own roster"),
    ("pause_dropout", "a joined player leaves from the pause menu, on its own pad"),
    ("leave_match", "LEAVE MATCH sends one F4 pulse and leaves the result overlay to the game (issue #22)"),
    ("two_human_match", "pad two drives its fighter in the FIGHT, not just at selection"),
    ("widescreen", "the composition follows the window, both directions, natives as negatives"),
    ("hidpi", "game and RmlUi use a SIMULATED 4K/200% drawable in a nested compositor (issues #56, #82)"),
    ("fullres", "the native engine targets 3840x1975 and covers both outer columns (issue #41)"),
    ("parallax_jitter", "full-resolution Lion Forest parallax advances every camera frame (issue #76)"),
    ("overlay_sampling", "the magnified pre-fight panel cannot sample its green source separators (issue #96)"),
    ("visibility", "procedural GPU probes prove character/caster visibility and shadow depth (issues #85 #97-#99)"),
    ("shadow_contact", "two real LF2 fighter silhouettes meet their projected shadows (issue #97)"),
    ("resize", "a resize leaves no stale pixels standing (issue #29)"),
    ("background", "the background override draws what the recompiled body drew, byte for byte"),
    ("caption", "the mode caption follows the view, with the recompiled body as control (issue #60)"),
    ("stage_mode", "the port reaches STAGE mode and the section lock holds the camera (issue #36)"),
    ("objects", "the stage's OBJECT pass is deterministic, with a skewed camera as the negative (issue #55)"),
    ("texture_cache", "a long Stage run reuses old GPU sheets without dropping later enemies (issue #83)"),
    ("render", "the GPU renderer draws what the software compositor draws (issue #30)"),
    ("sprite_passes", "the sprite sampling chain reproduces nearest exactly, then coarsens and outlines (issue #112)"),
    ("mesh", "the depth-tested geometry pass really tests depth (issues #49, #62)"),
    ("stage_geom", "a hand-woven .stage loads in the running game, at its layer's depth (issue #62)"),
)


def route_command(root: Path, name: str) -> list[str] | None:
    base = root / "tools" / "routes" / f"{name}_test"
    python_route = base.with_suffix(".py")
    if python_route.is_file():
        return [sys.executable, str(python_route)]
    shell_route = base.with_suffix(".sh")
    if shell_route.is_file():
        return ["sh", str(shell_route)]
    return None


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)

    selected = set(argv)
    known = {name for name, _ in ROUTES}
    unknown = selected - known
    if unknown:
        print(f"e2e: NOTHING RAN. Unknown route(s): {', '.join(sorted(unknown))}")
        print("    " + "\n    ".join(name for name, _ in ROUTES))
        return 2

    env = os.environ.copy()
    env.setdefault("BUILD", "scratch/build-clang")
    env.setdefault("GAME", "game")
    env["LF2_CONFIG"] = ""

    passed = failed = skipped = ran = 0
    failed_names: list[str] = []
    for name, _description in ROUTES:
        if selected and name not in selected:
            continue
        command = route_command(root, name)
        if command is None:
            print(f"MISSING  {name}: tools/routes/{name}_test.py or .sh does not exist")
            failed += 1
            failed_names.append(name)
            continue

        ran += 1
        print(f"\n=== {name} ===================================================", flush=True)
        result = subprocess.run(command, cwd=root, env=env, check=False)
        if result.returncode == 0:
            passed += 1
        elif result.returncode == 77:
            skipped += 1
            print("  SKIPPED (exit 77) -- this is NOT a pass")
        else:
            failed += 1
            failed_names.append(name)

    print()
    if ran == 0:
        print("e2e: NOTHING RAN. No route was selected")
        return 2
    print(f"e2e: {ran} script(s) -- {passed} passed, {failed} failed, {skipped} skipped")
    if skipped:
        print("     a skip means a required runtime input was missing; it proves nothing")
    if failed:
        print("     failed: " + " ".join(failed_names))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
