#!/bin/sh
# Drop-in coop: a second pad joins a match that is ALREADY running, and drives its fighter.
#
# Two-sided on purpose. "A fighter appeared" is not the claim -- the claim is that the pad
# CONTROLS it, and a fighter that appears and wanders under its own AI would satisfy a
# one-sided check exactly as well. So the same join is run twice, differing only in whether
# the pad presses a direction afterwards:
#
#   press  the pressed direction must reach the joined fighter's record
#   quiet  it must never reach it
#
# WHY NOT DISPLACEMENT, which is the obvious measure and is what this test used first: a
# fighter that joins mid-fight lands next to the brawl and gets knocked about, so an idle
# joiner drifted 56 px in one run and 69 in the next while a driven one managed ~120. No
# threshold separates those, and picking one that happened to pass would have been a test
# that lies. The claim splits cleanly in two instead:
#
#   here                  the pad's input reaches the JOINED fighter's record
#   two_human_match       the game turns input in a player record into movement -- measured
#                         on a fighter standing at its own start position, where
#                         displacement is clean (~1350 px against 0)
#
# Together those cover "the pad drives the fighter it joined". Neither is confounded by the
# fight moving things on its own.
#
# And both arms first assert that the join HAPPENED. Without that, a run whose scripted
# route never reached the match would sail through the `quiet` assertion -- a pad that
# joined nothing presses nothing into nothing, and that is the failure this test exists to
# catch.
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

# Pad two's first press is the join, and it must land INSIDE the match. The data load does
# not take a fixed number of frames, so a single press at a chosen frame sometimes arrives
# before the fight starts -- which it did, giving one arm a join and the other none, and a
# comparison between them would have been meaningless. Two join presses spread across the
# window make that miss unlikely.
#
# The second press is now also what LOCKS IN the character: a joiner gets a choice first
# (tools/coop_select_test.sh measures that part), and its pad's buttons are withheld from
# its fighter for as long as the choice is open. So both presses are load-bearing here, and
# the assertions below check the lock-in happened -- without it the "press" arm would find
# no direction in the record and read as a broken join rather than an unfinished choice.
#
# The measurement window is SHORT on purpose -- the watch's +5 and +120 samples -- and the
# directions cover it. Measuring over a long window instead made both arms meaningless: in a
# live fight an idle fighter gets knocked about, so over ~5 seconds the quiet arm drifted 56
# px while the pressed arm managed 102, and the two are not distinguishable. Displacement is
# only a clean signal while the fight has not had time to move things on its own.
JOIN="south:2300,south:2360"
PRESS="$JOIN,right:2380,right:2410,right:2440,right:2470,right:2500,right:2530"

run() {   # run <logfile> <pad2 script>
    ( cd "$GAME" && \
      SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
      LF2_VIRTUAL_PAD="$PAD1" LF2_VIRTUAL_PAD2="$2" LF2_COOP=1 LF2_COOP_CHAR=52 \
      LF2_QUIT_AFTER=2800 timeout 220 "$BUILD/lf2" lf2.exe ) > "$1" 2>&1
}

# Did the pressed direction ever reach the joined fighter's record? The watch accumulates
# the buttons it has seen since the join, so this does not depend on a press coinciding with
# a sample.
right_seen() {   # right_seen <logfile>
    grep "coop spawn: .* buttons seen:" "$1" | tail -1 | sed -n 's/.* right=\([0-9]*\) .*/\1/p'
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

for arm in press quiet; do
    log=$LOGP; [ "$arm" = quiet ] && log=$LOGQ
    if grep -q "coop select: slot .* LOCKED IN" "$log"; then
        echo "  ok    $arm: the joiner locked in a character, so its pad now drives it"
    else
        echo "  FAIL  $arm: the joiner never locked in, so its buttons are still withheld"
        echo "        and this arm measures an unfinished choice, not a join"
        fail=1
    fi
done

rp=$(right_seen "$LOGP"); rq=$(right_seen "$LOGQ")
if [ -z "${rp:-}" ] || [ -z "${rq:-}" ]; then
    echo "  FAIL  the spawn watch reported no button state in both arms"
    echo "        press: '${rp:-}'   quiet: '${rq:-}'"
    exit 1
fi

if [ "$rp" -eq 1 ]; then
    echo "  ok    press: the pressed direction reached the joined fighter's record"
else
    echo "  FAIL  press: the direction never reached the joined fighter -- it exists but the"
    echo "        pad is not wired to it"
    fail=1
fi
if [ "$rq" -eq 0 ]; then
    echo "  ok    quiet: with nothing pressed, no direction reached it"
else
    echo "  FAIL  quiet: a direction reached the fighter with nothing pressed, so the press"
    echo "        arm cannot be attributed to the pad"
    fail=1
fi

[ "$fail" = 0 ] && echo "drop-in coop: ok" || echo "drop-in coop: FAILED"
exit $fail
