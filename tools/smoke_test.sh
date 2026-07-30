#!/bin/sh
# End-to-end smoke test: drive the port deep into the game and assert the things that have
# actually broken before.
#
# What it does NOT guarantee is reaching a running match. The scripted keys reach character
# selection reliably, but whether the last presses land on "Fight!" or on "Reset Random"
# depends on when the pre-fight overlay opens relative to the ~13 s data load, and that
# varies between runs. Frame dumps confirmed runs that ended at character select while
# still reporting healthy blit and audio counts.
#
# That does not weaken the assertions: every one of them was checked against a
# deliberately broken build and failed there, so they detect the regressions they name
# regardless of which of the two screens the run ends on.
#
# Every assertion here corresponds to a real regression:
#   keyed blits   -- ADC/SBB dropped the carry, so DDBLT_KEYSRC was computed as 0 and
#                    every sprite drew in an opaque black box. Nothing else caught it.
#   audio peak    -- the mixer can run, be pulled from, and still emit pure silence.
#   music frames  -- background music decodes through ffmpeg; a broken path is silent.
#   sound plays   -- effects fire only once a match is running, not in the menus.
#   no aborts     -- unimplemented opcodes and stack faults abort by design.
#
# Thresholds are deliberately far below observed values (11k keyed blits, 8 plays) so this
# fails on "broken", not on "slightly different".
set -eu

# Resolved to absolute paths up front. The binary is launched from inside the game tree
# (the game opens its data by relative path), so a relative BUILD would have to be
# rewritten with a "../" prefix -- which silently breaks the moment BUILD is absolute, as
# it is when ctest passes CMAKE_BINARY_DIR.
BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# LF2_QUIT_AFTER makes the run end through the game's own shutdown instead of SIGTERM, so
# a hang or a crash on exit shows up as a non-zero status rather than being masked by the
# timeout that would have killed it anyway. timeout stays as a backstop.
#
# CPU time is captured because a regression to busy-waiting is invisible to every other
# assertion here: the game renders, sounds and plays correctly at 96% of a core, which is
# exactly how the unimplemented Sleep survived unnoticed.
TIMER=""
for t in /usr/bin/time /bin/time; do [ -x "$t" ] && TIMER=$t && break; done
CPUFILE=$(mktemp); trap 'rm -f "$LOG" "$CPUFILE"' EXIT

echo "running a match headless (about 75s)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_CK_DEBUG=1 LF2_AUDIO_DEBUG=1 LF2_SCREEN_HASH=1 \
  LF2_AUTOCLICK_ONCE=1 LF2_AUTOCLICK=403,228 LF2_AUTOCLICK_START=3000 \
  LF2_AUTOKEY_ONCE=1 \
  LF2_AUTOKEY=0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x68,0x68,0x65 \
  LF2_AUTOKEY_START=32000 LF2_AUTOKEY_EVERY=1800 \
  LF2_QUIT_AFTER=2100 \
  ${TIMER:+$TIMER -f "%U %S %e" -o "$CPUFILE"} \
  timeout 90 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1
rc=$?

fail=0
check() {   # check <description> <actual> <minimum>
    if [ "${2:-0}" -ge "$3" ]; then
        echo "  ok    $1: $2 (>= $3)"
    else
        echo "  FAIL  $1: $2 (want >= $3)"
        fail=1
    fi
}

last() { grep "^$1" "$LOG" | tail -1; }

# The leading-space anchor is load-bearing: without it "keyed blits=" also matches inside
# "unkeyed blits=", and taking the last match silently reads the unkeyed count -- which is
# large whether or not colour-keying works, so the assertion could never fail. That is
# exactly what this test did until it was checked against a deliberately broken build.
num() { echo "${1:-}" | grep -oE "(^|[[:space:]])$2=[0-9]+" | head -1 | cut -d= -f2; }

ck=$(last "colour-key:")
au=$(last "audio:")

check "screen transitions" "$(grep -c CHANGED "$LOG" || true)" 2
check "keyed blits"        "$(num "$ck" 'keyed blits')" 1000
check "sound effects"      "$(num "$au" plays)" 2
check "audio peak"         "$(num "$au" peak)" 1000
check "device pulls"       "$(num "$au" 'device-pulls')" 100

# Music is optional: it needs ffmpeg at runtime, so its absence must not fail the suite.
mf=$(num "$au" 'music-frames')
if command -v ffmpeg >/dev/null 2>&1; then
    check "music frames" "$mf" 100000
else
    echo "  skip  music frames: ffmpeg not on PATH (music is an optional dependency)"
fi

# Busy-wait guard. Observed ~13% with Sleep honoured and ~96% without, so 50% separates
# the two by a wide margin without being sensitive to machine speed.
if [ -n "$TIMER" ] && [ -s "$CPUFILE" ]; then
    # The run always ends via timeout, so /usr/bin/time prefixes a "Command exited with
    # non-zero status" line. Select the timing line by shape rather than by position.
    pct=$(awk 'NF == 3 && $3 + 0 > 0 { printf "%d\n", ($1 + $2) * 100 / $3 }' "$CPUFILE" \
          | tail -1)
    if [ "${pct:-100}" -lt 50 ]; then
        echo "  ok    cpu usage: ${pct}% of one core (< 50)"
    else
        echo "  FAIL  cpu usage: ${pct}% of one core -- looks like a busy-wait"
        fail=1
    fi
else
    echo "  skip  cpu usage: no /usr/bin/time available"
fi

# The run now ends through the game's own shutdown, so a non-zero status means something
# actually went wrong rather than "timeout killed it", which is what it always meant before.
if [ "$rc" -eq 0 ]; then
    echo "  ok    exit status: 0 (clean shutdown)"
else
    echo "  FAIL  exit status: $rc ($([ "$rc" -eq 124 ] && echo 'timed out -- never reached LF2_QUIT_AFTER' || echo 'crashed or aborted'))"
    fail=1
fi

if grep -qE "unimplemented opcode|fell off the end|Aborted" "$LOG"; then
    echo "  FAIL  aborts: found in output"; grep -E "unimplemented opcode|fell off the end" "$LOG" | head -3
    fail=1
else
    echo "  ok    aborts: none"
fi

[ "$fail" -eq 0 ] && echo "smoke test PASSED" || echo "smoke test FAILED"
exit "$fail"
