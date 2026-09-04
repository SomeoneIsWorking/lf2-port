#!/usr/bin/env python3
"""Greedily extend a click path through LF2's menus.

The menus are mouse-driven. This tries every recovered candidate center as the
next click and keeps whichever produces one more screen
transition than the current path, which is the only observable that says "something
happened" -- the key array reads the same on every screen, so it cannot be used here.

Screen transitions are counted by runtime/video/ddraw.c's LF2_SCREEN_HASH, which compares a
subsampled framebuffer signature and reports a change only when a large fraction differs,
so menu animation does not register.

Clicks are one-shot (LF2_AUTOCLICK_ONCE): cycling the list walks back out of the menu and
shows up as the game oscillating between two screens.
"""
import subprocess, sys, os

GAME = os.path.join(os.path.dirname(__file__), '..', 'game')
LF2  = os.path.join('..', 'scratch', 'build', 'lf2')

CANDIDATES = ["403,287", "403,317", "403,348", "397,373", "314,369", "486,369",
              "293,486", "470,216", "480,428", "579,493"]

STEP_MS, START_MS = 1700, 2800

def changes(path):
    """Screen transitions observed for a click path."""
    secs = START_MS / 1000 + len(path) * STEP_MS / 1000 + 4
    env = dict(os.environ,
               SDL_VIDEODRIVER='offscreen', LF2_SCREEN_HASH='1',
               LF2_AUTOCLICK_ONCE='1', LF2_AUTOCLICK=';'.join(path),
               LF2_AUTOKEY_START=str(START_MS), LF2_AUTOKEY_EVERY=str(STEP_MS))
    try:
        out = subprocess.run(['timeout', str(int(secs)), LF2, 'lf2.exe'], cwd=GAME,
                             env=env, capture_output=True, text=True, timeout=secs + 15)
    except subprocess.TimeoutExpired:
        return -1
    return out.stderr.count('CHANGED')

def main():
    path = sys.argv[1].split(';') if len(sys.argv) > 1 else []
    best = changes(path) if path else 0
    print(f"start: {';'.join(path) or '(empty)'} -> {best} changes", flush=True)

    while True:
        gained = None
        for c in CANDIDATES:
            if path and c == path[-1]:
                continue                      # clicking the same spot twice is a no-op
            n = changes(path + [c])
            print(f"   try +{c}: {n}", flush=True)
            if n > best:
                gained, best = c, n
                break                         # first improvement: the search is expensive
        if not gained:
            print(f"\nno candidate extends the path; stalled at {best} changes")
            print(f"FINAL LF2_AUTOCLICK={';'.join(path)}")
            return
        path.append(gained)
        print(f"-> path {';'.join(path)} = {best} changes\n", flush=True)

main()
