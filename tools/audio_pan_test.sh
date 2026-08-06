#!/bin/sh
# Widescreen does not silence the right of the picture (issue #39).
#
# The game pans a sound between two speakers placed on the SCREEN, at x 200 and x 600, each
# audible out to 400 px (runtime/overrides/audio_pan.c has the 211 bytes it was read from).
# Those numbers are the 794 screen written down as pixels, so the audible span is -200..1000:
# wider than the game's own picture, which is why nothing is ever culled at 794 and why nobody
# had reason to look at the function.
#
# Widen the view and the span does not move. At 1920 everything past screen x 1000 is outside
# both speakers and its volume is exactly zero -- the right 48% of the picture, silent.
#
# THREE ARMS, and the third is the one that gives the other two meaning:
#
#   794x550    the speakers must be at EXACTLY 200 and 600. This is the game unchanged, and it
#              is why the port SCALES the constants rather than re-deriving them from the view
#              (view/4 would give 198, not the 200 that shipped).
#   1920x1080  the audible span must cover the whole picture.
#   1920x1080, LF2_AUDIO_PAN_RAW=1
#              the same window with the scaling turned OFF must FAIL to cover it. Without this
#              arm, "the span covers the picture" would pass on a build whose span was simply
#              always enormous, and the test would be measuring nothing.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

PAD="south:900,south:960,south:1020,south:1080"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"

# run <window> [VAR=value ...] -- reaches a match so sounds are actually panned
run() {
    win=$1; shift
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_WINDOW_SIZE="$win" \
          LF2_AUDIO_PAN=1 LF2_VIRTUAL_PAD="$PAD" LF2_QUIT_AFTER=2750 "$@" \
          timeout 400 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true
}

# The report's absence is a failure in itself: no line means no sound was ever panned, which
# is a different thing from panning it wrongly and must not read as a pass.
span_line() {
    line=$(grep -m1 "audio pan: view" "$LOG" || true)
    if [ -z "$line" ]; then
        say_fail "$1: no pan was reported at all, so NOTHING was measured"
        grep -m3 -i "audio pan" "$LOG" || echo "        no audio pan output at all"
        return 1
    fi
    return 0
}

echo "widescreen does not silence the right of the picture: three runs, about 4 minutes..."

run 794x550
if span_line "794x550"; then
    if grep -q "speakers at 200 and 600" "$LOG"; then
        say_ok "794x550: speakers at exactly 200 and 600 -- the game's own constants"
    else
        say_fail "794x550: $(grep -m1 'audio pan: view' "$LOG")"
        say_fail "      (expected 'speakers at 200 and 600' -- the port must not change the"
        say_fail "       game at its own width)"
    fi
    if grep -q "covers the whole 794-wide picture" "$LOG"; then
        say_ok "794x550: nothing on screen is culled"
    else
        say_fail "794x550: the audible span does not cover the game's own picture"
    fi
fi

run 1920x1080
if span_line "1920x1080"; then
    if grep -q "covers the whole 1920-wide picture" "$LOG"; then
        say_ok "1920x1080: the audible span covers the whole picture"
    else
        say_fail "1920x1080: $(grep -m1 'audio pan:.*SILENT' "$LOG" || echo 'span does not cover')"
    fi
fi

run 1920x1080 LF2_AUDIO_PAN_RAW=1
if span_line "1920x1080 unscaled"; then
    if grep -q "does NOT cover the picture" "$LOG"; then
        say_ok "1920x1080 with the scaling OFF: the picture IS silenced on the right, so the"
        say_ok "      arm above can fail"
    else
        say_fail "1920x1080 with the scaling OFF the span still covered the picture -- this"
        say_fail "      test cannot distinguish a fixed build from a broken one, so the pass"
        say_fail "      above proves nothing"
    fi
fi

[ "$fail" = 0 ] && echo "audio pan: ok" || echo "audio pan: FAILED"
exit $fail
