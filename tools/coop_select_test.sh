#!/bin/sh
# A late joiner CHOOSES its character: the fighter flashes, left/right cycle the game's own
# roster, and attack locks in.
#
# One run, four claims, and each of them is one a broken implementation could satisfy by
# accident if it were asserted on its own:
#
#   roster   the cycle runs over the GAME's characters, more than one of them. A build that
#            failed to read the registry would still "cycle" over a list of one, and every
#            other assertion here would pass while the player had no choice at all.
#   flash    the fighter's gate byte really goes down and up again. This is the one part of
#            the feature that is a schedule rather than a press, so it is the part that can
#            silently not happen -- and a fighter that never flashes looks, from outside,
#            exactly like one that does.
#   cycle    right ADVANCES and left goes BACK to where right came from. Asserting only
#            that the id changed would pass for a build that ignored the direction, and
#            asserting only on right would pass for one that cycled on any press at all.
#   lock     the character that plays is the one that was on screen when attack was
#            pressed. That is the whole point of choosing, and it is the claim that fails
#            if lock-in re-picks rather than keeping.
#
# The negatives are asserted too: while the joiner is choosing, its pad's directions must
# NOT reach the fighter's record. Left and right are choosing a character there, and a
# build that also fed them to the fighter would have it walking off while its player chose.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Pad one takes the usual deterministic route into a VS match; see tools/controller_test.sh
# for how the pre-fight overlay was made reproducible.
PAD1="south:900,south:960,south:1020,south:1080,south:1140,south:1200,south:1260,south:1320"
PAD1="$PAD1,up:1380,up:1440,south:1500,south:1700,south:1920,up:2020,up:2080,south:2140"
PAD1="$PAD1,right:2250,south:2300"

# Pad two: one press to claim and open the choice, a long enough gap for the flash to run
# several cycles, then right twice, left once, and attack to lock in.
#
# Only ONE join press, unlike tools/coop_dropin_test.sh, and that matters: a second attack
# would be a lock-in, and this run needs the selection to still be open when the directions
# arrive. The cost is that a run whose scripted route missed the match window proves
# nothing -- so the first assertion below is that the selection opened at all.
PAD2="south:2300,right:2400,right:2440,left:2480,south:2560"

echo "coop select: a late joiner picks a character (about 2 min)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_VIRTUAL_PAD="$PAD1" LF2_VIRTUAL_PAD2="$PAD2" LF2_COOP=1 \
  LF2_QUIT_AFTER=2800 timeout 220 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

open_line=$(grep -m1 "coop select: slot .* is choosing" "$LOG" || true)
if [ -z "$open_line" ]; then
    echo "  FAIL  no character selection opened, so this run proves NOTHING about choosing"
    echo "        (most likely the scripted route never reached a running match)"
    grep -m3 "^coop" "$LOG" || echo "        no coop output at all"
    exit 1
fi
say_ok "the selection opened: $open_line"

# roster -- more than one character to choose between, from the game's own registry.
n=$(printf '%s\n' "$open_line" | sed -n 's/.* -- \([0-9]*\) characters on the roster.*/\1/p')
if [ -n "${n:-}" ] && [ "$n" -gt 1 ]; then
    say_ok "roster: $n playable characters were read from the game's own registry"
else
    say_fail "roster: '${n:-}' characters -- a cycle over one character is not a choice"
fi

# flash -- the gate really went down and came back up.
hidden=$(grep -c "coop select: slot .* -- hidden" "$LOG" || true)
shown=$(grep -c "coop select: slot .* -- shown" "$LOG" || true)
if [ "${hidden:-0}" -ge 1 ] && [ "${shown:-0}" -ge 1 ]; then
    say_ok "flash: the fighter went out of the world $hidden time(s) and back in $shown"
else
    say_fail "flash: hidden=$hidden shown=$shown -- the joiner never flashed, so nothing on"
    say_fail "       screen distinguished it from a fighter already in the fight"
fi

# cycle -- right advances, left returns to where right came from.
c1=$(grep -m1 "cycled right, id" "$LOG" | sed -n 's/.*id \([0-9]*\) -> \([0-9]*\).*/\1 \2/p')
c2=$(grep    "cycled right, id" "$LOG" | sed -n '2p' | sed -n 's/.*id \([0-9]*\) -> \([0-9]*\).*/\1 \2/p')
cl=$(grep -m1 "cycled left, id"  "$LOG" | sed -n 's/.*id \([0-9]*\) -> \([0-9]*\).*/\1 \2/p')
if [ -z "${c1:-}" ] || [ -z "${c2:-}" ] || [ -z "${cl:-}" ]; then
    say_fail "cycle: expected two right presses and one left to be reported, got"
    say_fail "       right='${c1:-}' right='${c2:-}' left='${cl:-}'"
else
    r1_from=${c1% *}; r1_to=${c1#* }
    r2_from=${c2% *}; r2_to=${c2#* }
    l_from=${cl% *};  l_to=${cl#* }
    if [ "$r1_to" = "$r2_from" ] && [ "$r2_to" = "$l_from" ] && [ "$l_to" = "$r2_from" ]; then
        say_ok "cycle: right $r1_from->$r1_to->$r2_to, left back to $l_to -- left undoes right"
    else
        say_fail "cycle: right $r1_from->$r1_to then $r2_from->$r2_to, left $l_from->$l_to."
        say_fail "       left did not return to $r2_from, so the two directions are not opposites"
    fi
fi

# lock -- what plays is what was on screen.
lock=$(grep -m1 "LOCKED IN character id" "$LOG" | sed -n 's/.*LOCKED IN character id \([0-9]*\) .*/\1/p')
if [ -z "${lock:-}" ]; then
    say_fail "lock: the selection never closed -- the joiner is still choosing"
elif [ -n "${cl:-}" ] && [ "$lock" = "${cl#* }" ]; then
    say_ok "lock: character $lock locked in -- the one the last press had left on screen"
else
    say_fail "lock: locked in $lock but the last cycle left ${cl#* } on screen, so the"
    say_fail "      fighter that plays is not the one that was chosen"
fi

# The negative: while choosing, the pad's directions must not reach the fighter's record.
# The watch is latched at the LOCK-IN, so what it accumulates is the fighter's playing life
# -- and the run presses nothing after locking in, so any direction in there arrived while
# the player was still choosing.
seen=$(grep "coop spawn: .* buttons seen:" "$LOG" | tail -1 || true)
if [ -z "$seen" ]; then
    say_fail "withheld: the spawn watch reported no button state, so the negative is untested"
else
    l=$(printf '%s\n' "$seen" | sed -n 's/.* left=\([0-9]*\) .*/\1/p')
    r=$(printf '%s\n' "$seen" | sed -n 's/.* right=\([0-9]*\) .*/\1/p')
    if [ "${l:-1}" = 0 ] && [ "${r:-1}" = 0 ]; then
        say_ok "withheld: no direction reached the fighter's record while it was being chosen"
    else
        say_fail "withheld: left=$l right=$r reached the record -- the joiner was walking"
        say_fail "          about while its player was still choosing a character"
    fi
fi

[ "$fail" = 0 ] && echo "coop select: ok" || echo "coop select: FAILED"
exit $fail
