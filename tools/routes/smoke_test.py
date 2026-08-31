#!/usr/bin/env python3
"""Drive the port into a match and check previously regressed runtime behavior.

Inputs are anchored to screens rather than absolute frames because data-load duration and host
load move screen arrival times (issues #18 and #25). The deliberately loose render/audio
thresholds distinguish broken output from normal run-to-run variation.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import is_record, payload_line, payload_text


KEY_SCRIPT = (
    "0x5A@modemenu+60,"
    "0x5A@charselect+58,0x5A@charselect+118,0x5A@charselect+178,"
    "0x5A@charselect+238,0x5A@charselect+298,0x5A@charselect+358,"
    "0x26@charselect+418,0x26@charselect+478,0x5A@charselect+538,"
    "0x5A@charselect+738,0x5A@overlay+219,0x26@overlay+319,"
    "0x26@overlay+379,0x5A@overlay+439,0x27@match+108,0x5A@match+158"
)
NATIVE_ENTRY = "startup: entering native port entry (guest PE entry and WinMain bypassed)"
LOAD_BEGIN = "startup: native data initialization begin"
LOAD_COMPLETE = "startup: native data initialization complete"
RETIRED_STARTUP_MESSAGES = (
    "entering at 00445560",
    "startup: loading game data synchronously",
    "startup: world constructed in local-loader state",
    "startup: data loaded; presenting the mode menu",
    "startup: first menu frame presented; window revealed",
)


def resolved_environment_path(name: str, default: str) -> Path:
    return Path(os.environ.get(name) or default).resolve()


def messages_appear_in_order(log: str, *messages: str) -> bool:
    position = -1
    for message in messages:
        position = log.find(f"{message}\n", position + 1)
        if position < 0:
            return False
    return True


def last_line_starting_with(log: str, prefix: str) -> str:
    return next(
        (line for line in reversed(log.splitlines()) if line.startswith(prefix)), ""
    )


def metric(line: str, name: str) -> int:
    # The leading boundary keeps "keyed blits" from matching "unkeyed blits". Without it,
    # the colour-key check reads a large count even when keyed rendering is broken.
    match = re.search(rf"(?:^|\s){re.escape(name)}=([0-9]+)", line)
    return int(match.group(1)) if match else 0


def check_minimum(description: str, actual: int, minimum: int) -> bool:
    passed = actual >= minimum
    if passed:
        print(f"  ok    {description}: {actual} (>= {minimum})")
    else:
        print(f"  FAIL  {description}: {actual} (want >= {minimum})")
    return passed


def cpu_percentage(cpu_log: Path) -> int | None:
    if not cpu_log.is_file():
        return None
    percentage = None
    for line in cpu_log.read_text(errors="replace").splitlines():
        fields = line.split()
        if len(fields) != 3:
            continue
        try:
            user, system, elapsed = (float(field) for field in fields)
        except ValueError:
            continue
        if elapsed > 0:
            percentage = int((user + system) * 100 / elapsed)
    return percentage


def run_smoke(
    build: Path, game: Path, log_path: Path, cpu_log: Path
) -> tuple[int, bool]:
    timer = next(
        (candidate for candidate in (Path("/usr/bin/time"), Path("/bin/time"))
         if os.access(candidate, os.X_OK)),
        None,
    )
    command: list[str] = []
    if timer is not None:
        command.extend([str(timer), "-f", "%U %S %e", "-o", str(cpu_log)])
    command.extend(
        ["timeout", "-k", "5", "150", str(build / "lf2"), "lf2.exe"]
    )
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER="offscreen",
        SDL_AUDIODRIVER="dummy",
        LF2_CK_DEBUG="1",
        LF2_AUDIO_DEBUG="1",
        LF2_SCREEN_HASH="1",
        LF2_KEY_SCRIPT=KEY_SCRIPT,
        LF2_QUIT_AFTER="2160",
    )
    print("running a match headless (about 90s)...")
    with log_path.open("w") as output:
        result = subprocess.run(
            command,
            cwd=game,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
            check=False,
        )
    return result.returncode, timer is not None


def check_results(
    log_path: Path, cpu_log: Path, returncode: int, timer_used: bool
) -> int:
    raw_log = log_path.read_text(errors="replace")
    log = payload_text(raw_log)
    failed = False
    colour_key = last_line_starting_with(log, "colour-key:")
    audio = last_line_starting_with(log, "audio:")

    checks = (
        ("screen transitions", sum("CHANGED" in line for line in log.splitlines()), 2),
        ("keyed blits", metric(colour_key, "keyed blits"), 1000),
        ("sound effects", metric(audio, "plays"), 2),
        ("audio peak", metric(audio, "peak"), 1000),
        ("device pulls", metric(audio, "device-pulls"), 100),
    )
    for description, actual, minimum in checks:
        failed |= not check_minimum(description, actual, minimum)

    screen_match = re.search(
        r"^scripted input: screen ([^ ]+) first", log, flags=re.MULTILINE
    )
    first_screen = screen_match.group(1) if screen_match else ""
    startup_ok = (
        first_screen == "modemenu"
        and messages_appear_in_order(log, NATIVE_ENTRY, LOAD_BEGIN, LOAD_COMPLETE)
    )
    if startup_ok:
        print("  ok    startup: native entry initializes data before the mode menu")
    else:
        print(
            f"  FAIL  startup: first visible screen is '{first_screen or 'none'}', "
            "expected modemenu"
        )
        print("        (native-entry and data-init begin/complete markers are required)")
        failed = True

    timestamped_entry = any(
        is_record(line) and payload_line(line) == NATIVE_ENTRY
        for line in raw_log.splitlines()
    )
    if timestamped_entry:
        print("  ok    logs: native entry is a timestamped Lucent record")
    else:
        print("  FAIL  logs: native entry has no Lucent UTC timestamp")
        failed = True

    match_reached = "scripted input: screen match first up at frame " in log
    all_keys_fired = "LF2_KEY_SCRIPT: 17 of 17 items fired\n" in log
    if match_reached and all_keys_fired:
        print("  ok    route: reached a match and fired all 17 screen-anchored keys")
    else:
        print("  FAIL  route: did not reach a match with all 17 keys fired")
        failed = True

    retired = [message for message in RETIRED_STARTUP_MESSAGES if message in log]
    if retired:
        print("  FAIL  startup: retired delayed-reveal/loading-state message found")
        for message in retired:
            print(f"        {message}")
        failed = True
    else:
        print("  ok    startup: no delayed-reveal/loading-state path was reported")

    music_frames = metric(audio, "music-frames")
    if shutil.which("ffmpeg") is not None:
        failed |= not check_minimum("music frames", music_frames, 100000)
    else:
        print("  skip  music frames: ffmpeg not on PATH (music is optional)")

    percentage = cpu_percentage(cpu_log) if timer_used else None
    if percentage is None:
        print("  skip  cpu usage: no /usr/bin/time result available")
    elif percentage < 50:
        print(f"  ok    cpu usage: {percentage}% of one core (< 50)")
    else:
        print(f"  FAIL  cpu usage: {percentage}% of one core -- looks like a busy-wait")
        failed = True

    if returncode == 0:
        print("  ok    exit status: 0 (clean shutdown)")
    else:
        reason = (
            "timed out -- never reached LF2_QUIT_AFTER"
            if returncode == 124
            else "crashed or aborted"
        )
        print(f"  FAIL  exit status: {returncode} ({reason})")
        failed = True

    abort_pattern = re.compile(r"unimplemented opcode|fell off the end|Aborted")
    aborts = [line for line in log.splitlines() if abort_pattern.search(line)]
    if aborts:
        print("  FAIL  aborts: found in output")
        for line in aborts[:3]:
            print(line)
        failed = True
    else:
        print("  ok    aborts: none")

    print(f"smoke test {'FAILED' if failed else 'PASSED'}")
    return int(failed)


def main() -> int:
    build = resolved_environment_path("BUILD", "build/clang")
    game = resolved_environment_path("GAME", "game")
    log_dir = resolved_environment_path("LF2_SCRATCH", "scratch") / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "smoke.log"
    cpu_log = log_dir / "smoke-time.log"

    if not os.access(build / "lf2", os.X_OK):
        print(f"SKIP: {build / 'lf2'} not built")
        return 77
    if not (game / "lf2.exe").is_file():
        print(f"SKIP: no game tree at {game}")
        return 77

    returncode, timer_used = run_smoke(build, game, log_path, cpu_log)
    return check_results(log_path, cpu_log, returncode, timer_used)


if __name__ == "__main__":
    raise SystemExit(main())
