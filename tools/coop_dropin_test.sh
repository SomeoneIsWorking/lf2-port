#!/bin/sh
# Drop-in coop: a second pad joins a match that is ALREADY running, and drives its fighter.
#
# Two-sided on purpose. "A fighter appeared" is not the claim -- the claim is that the pad
# CONTROLS it, and a fighter that appears and wanders under its own AI would satisfy a
# one-sided check exactly as well. So the same join is run twice, differing only in whether
# the pad presses a direction afterwards:
#
#   press  the joined fighter must travel a real distance
#   quiet  it must stay put
#
# And both arms first assert that the join HAPPENED. Without that, a run whose scripted
# route never reached the match would sail through the `quiet` assertion -- a fighter that
# does not exist does not move either, and that is the failure this test exists to catch.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOGP=$(mktemp); LOGQ=$(mktemp)
trap 'rm -f "$LOGP" "$LOGQ"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Pad one takes the usual deterministic route into a VS match (see tools/controller_test.sh
# for how the pre-fight overlay was made reproducible).
PAD1="south:900,south:960,south:1020,south:1080,south:1140,south:1200,south:1260,south:1320"
PAD1="$PAD1,up:1380,up:1440,south:1500,south:1700,south:1920,up:2020,up:2080,south:2140"
PAD1="$PAD1,right:2250,south:2300"

# Pad two presses for the first time at 2350, well after the match starts. That press is the
# join; the rights after it are what must move the fighter.
JOIN="south:2350"
PRESS="$JOIN,right:2400,right:2430,right:2460,right:2490,right:2520,right:2550"

run() {   # run <logfile> <pad2 script>
    ( cd "$GAME" && \
      SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
      LF2_VIRTUAL_PAD="$PAD1" LF2_VIRTUAL_PAD2="$2" LF2_COOP=52 \
      LF2_QUIT_AFTER=2650 timeout 200 "$BUILD/lf2" lf2.exe ) > "$1" 2>&1
}

# x of the joined fighter at a given age, from the spawn watch's own report.
xat() {   # xat <logfile> <age>
    grep "coop spawn: + *$2 frames" "$1" | sed -n 's/.* x=\([0-9-]*\) .*/\1/p' | head -1
}

echo "drop-in coop: pad two joins a running match (about 3 min for both arms)..."
run "$LOGP" "$PRESS"
run "$LOGQ" "$JOIN"

fail=0
for arm in press quiet; do
    log=$LOGP; [ "$arm" = quiet ] && log=$LOGQ
    if grep -q "^coop: device .* claimed player slot .* mid-match" "$log"; then
        echo "  ok    $arm: the second pad joined a running match"
    else
        echo "  FAIL  $arm: no mid-match join happened, so this run proves nothing"
        echo "        (most likely the scripted route never reached the match)"
        fail=1
    fi
done

x0p=$(xat "$LOGP" 5);   x1p=$(xat "$LOGP" 120)
x0q=$(xat "$LOGQ" 5);   x1q=$(xat "$LOGQ" 120)

if [ -z "${x0p:-}" ] || [ -z "${x1p:-}" ] || [ -z "${x0q:-}" ] || [ -z "${x1q:-}" ]; then
    echo "  FAIL  the spawn watch did not report a position in both arms"
    echo "        press: '${x0p:-}' -> '${x1p:-}'   quiet: '${x0q:-}' -> '${x1q:-}'"
    exit 1
fi

dp=$(( x1p - x0p )); [ "$dp" -lt 0 ] && dp=$(( -dp ))
dq=$(( x1q - x0q )); [ "$dq" -lt 0 ] && dq=$(( -dq ))

# The threshold sits between the two measured behaviours: a driven fighter covers ~180 px
# over these frames, an undriven one drifts under 10 while it lands.
if [ "$dp" -ge 60 ]; then
    echo "  ok    press: the joined fighter moved $dp px under the pad"
else
    echo "  FAIL  press: the joined fighter moved only $dp px (want >= 60) -- it exists but"
    echo "        the pad is not driving it"
    fail=1
fi
if [ "$dq" -le 30 ]; then
    echo "  ok    quiet: with no direction pressed it stayed put ($dq px)"
else
    echo "  FAIL  quiet: it moved $dq px with nothing pressed, so the movement in the press"
    echo "        arm cannot be attributed to the pad"
    fail=1
fi

[ "$fail" = 0 ] && echo "drop-in coop: ok" || echo "drop-in coop: FAILED"
exit $fail
