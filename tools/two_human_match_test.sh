#!/bin/sh
# Two humans in a MATCH: the second pad's fighter is at its own object index, and the pad
# drives it.
#
# tools/controller_2p_test.sh proves a second pad JOINS at character selection -- it asserts
# the word "Computer" is never drawn -- but it quits at frame 1900 and never reaches a
# match. So "a second human's fighter is actually driven once the fight starts" had no
# coverage at all, and the port could have shipped a second player who joins and then cannot
# move.
#
# Two-sided, for the same reason coop_dropin is: a fighter that exists and drifts would
# satisfy a one-sided movement check. The same route is run twice, differing only in whether
# pad two presses a direction once the match is running.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOGP=$(mktemp); LOGQ=$(mktemp)
trap 'rm -f "$LOGP" "$LOGQ"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Pad one's route into a VS match. Pad two joins at 1250 (the window controller_2p_test
# measured) and then needs TWO more presses of its own: character selection asks each joined
# player for a Fighter and then a Team, and player one cannot proceed until every joined
# player has finished. Without those, the screen sits there with both players joined and
# nothing happening -- which is what the first attempts at this route did.
PAD1="south:900,south:960,south:1020,south:1080"        # the front end, before any screen
PAD1="$PAD1,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238,up@charselect+298,up@charselect+358"
PAD1="$PAD1,south@charselect+418,south@charselect+618,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"
JOIN="south@charselect+168,south@charselect+298,south@charselect+478"
PRESS="$JOIN,right@match+108,right@match+138,right@match+168,right@match+198,right@match+228,right@match+258"

run() {   # run <logfile> <pad2 script>
    ( cd "$GAME" && \
      SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
      LF2_VIRTUAL_PAD="$PAD1" LF2_VIRTUAL_PAD2="$2" \
      LF2_COOP_TABLE=live+60 LF2_COOP_TRACK=1 \
      LF2_QUIT_AFTER=2450 timeout 200 "$BUILD/lf2" lf2.exe ) > "$1" 2>&1
}

echo "two humans in a match: pad two joins at character selection, two arms..."
run "$LOGP" "$PRESS"
run "$LOGQ" "$JOIN"

fail=0

# Player two's fighter must be at OBJECT INDEX 1. This is the dynamic half of the claim the
# game's own gather makes statically -- it walks the device-selector table and the object
# pointer table in lockstep, so a human player's fighter has to be at its own index.
for arm in press quiet; do
    log=$LOGP; [ "$arm" = quiet ] && log=$LOGQ
    if grep -qE "^  \[  1\] [0-9a-f]+ gate=1 +LIVE" "$log"; then
        echo "  ok    $arm: player two's fighter is in the world at object index 1"
    else
        echo "  FAIL  $arm: no live fighter at object index 1 -- either the route never"
        echo "        reached a match with two humans, or player two landed elsewhere"
        fail=1
    fi
done

# Movement of entry 1, from the tracker's own report: first and last sample in the match.
xfirst() { grep "coop track: .* entry 1 x=" "$1" | head -1 | sed -n 's/.* x=\([0-9-]*\) .*/\1/p'; }
xlast()  { grep "coop track: .* entry 1 x=" "$1" | tail -1 | sed -n 's/.* x=\([0-9-]*\) .*/\1/p'; }

ap=$(xfirst "$LOGP"); bp=$(xlast "$LOGP")
aq=$(xfirst "$LOGQ"); bq=$(xlast "$LOGQ")
if [ -z "${ap:-}" ] || [ -z "${bp:-}" ] || [ -z "${aq:-}" ] || [ -z "${bq:-}" ]; then
    echo "  FAIL  the tracker reported no position for entry 1 in both arms"
    echo "        press: '${ap:-}'..'${bp:-}'   quiet: '${aq:-}'..'${bq:-}'"
    exit 1
fi

dp=$(( bp - ap )); [ "$dp" -lt 0 ] && dp=$(( -dp ))
dq=$(( bq - aq )); [ "$dq" -lt 0 ] && dq=$(( -dq ))

# Measured: ~300 px under the pad, 0 without it. The threshold sits well between.
if [ "$dp" -ge 100 ]; then
    echo "  ok    press: player two's fighter moved $dp px under pad two"
else
    echo "  FAIL  press: player two's fighter moved only $dp px (want >= 100) -- it joined"
    echo "        the match but the pad is not driving it"
    fail=1
fi
if [ "$dq" -le 40 ]; then
    echo "  ok    quiet: with no direction pressed it stayed put ($dq px)"
else
    echo "  FAIL  quiet: it moved $dq px with nothing pressed, so the movement in the press"
    echo "        arm cannot be attributed to pad two"
    fail=1
fi

[ "$fail" = 0 ] && echo "two humans in a match: ok" || echo "two humans in a match: FAILED"
exit $fail
