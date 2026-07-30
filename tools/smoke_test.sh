#!/bin/sh
# End-to-end smoke test: drive the port to a running match and assert the things that
# have actually broken before.
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

echo "running a match headless (about 75s)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_CK_DEBUG=1 LF2_AUDIO_DEBUG=1 LF2_SCREEN_HASH=1 \
  LF2_AUTOCLICK_ONCE=1 LF2_AUTOCLICK=403,228 LF2_AUTOCLICK_START=3000 \
  LF2_AUTOKEY_ONCE=1 \
  LF2_AUTOKEY=0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x68,0x68,0x65 \
  LF2_AUTOKEY_START=32000 LF2_AUTOKEY_EVERY=1800 \
  timeout 75 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true

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

if grep -qE "unimplemented opcode|fell off the end|Aborted" "$LOG"; then
    echo "  FAIL  aborts: found in output"; grep -E "unimplemented opcode|fell off the end" "$LOG" | head -3
    fail=1
else
    echo "  ok    aborts: none"
fi

[ "$fail" -eq 0 ] && echo "smoke test PASSED" || echo "smoke test FAILED"
exit "$fail"
