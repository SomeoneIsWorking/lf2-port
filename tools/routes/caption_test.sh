#!/bin/sh
# The mode caption follows the view, and is byte-for-byte the game's own at 794 (issue #60).
#
# fn_0041b130 assembles "Stage mode (Difficult)" out of the game's strings and draws it
# RIGHT-ANCHORED -- `x = 790 - 8*len` -- against the game's own 794-wide screen. The port owns
# that function now and anchors to the VIEW instead. Two things therefore have to be true, and
# neither is checkable from the other:
#
#   identity  at a 794 view the port's caption must be INDISTINGUISHABLE from the recompiled
#             body's -- same x, same y, same string. The string assembly is the risky half of
#             the hand-port (five mode strings, four difficulty suffixes, a Survival special
#             case) and an x formula that is right about a wrong string would still look right.
#             LF2_CAPTION_ORIG=1 runs the original body, so this is a real A/B and not the port
#             compared with itself -- the failure that made tools/e2e.sh objects vacuous once.
#   follows   at a 1600 view the port's caption must move to the view's right edge and the
#             ORIGINAL body's must not. The second half is the control: without it, "the x
#             changed" could be anything about the wider run.
#
# LF2_GAMETEXT_DEBUG prints the arguments the draw actually receives, so this reads the layout
# rather than counting pixels of a glyph -- which also means it says WHICH string moved.
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

PAD="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"

# run <window> <orig?>  -> the caption's gametext line, or empty
cap() {
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft \
          LF2_WINDOW_SIZE="$1" ${2:+LF2_CAPTION_ORIG=1} LF2_GAMETEXT_DEBUG=1 \
          LF2_MODE=stage LF2_VIRTUAL_PAD="$PAD" LF2_QUIT_AFTER=2160 \
          timeout -k 5 400 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true
    # The outline wrapper draws the string four times at +-1, so the run has four lines per
    # frame that differ only in x and y. The FIRST is the one whose x is the layout's own.
    grep -m1 'gametext .*mode' "$LOG" || true
}

echo "caption: the mode caption follows the view (issue #60)..."

p794=$(cap 794x550 "")
o794=$(cap 794x550 1)

if [ -z "$p794" ] || [ -z "$o794" ]; then
    echo "  FAIL  no mode caption was drawn at 794 in $( [ -z "$p794" ] && echo "the port's arm" )"
    echo "        $( [ -z "$o794" ] && echo "the original's arm" ) -- the route did not reach"
    echo "        stage mode, so nothing below would mean anything"
    exit 1
fi

if [ "$p794" = "$o794" ]; then
    say_ok "identity: at 794 the port draws exactly what the recompiled body draws"
    say_ok "          $p794"
else
    say_fail "identity: at 794 the port and the recompiled body disagree"
    say_fail "          port: $p794"
    say_fail "          orig: $o794"
fi

p1600=$(cap 1600x550 "")
o1600=$(cap 1600x550 1)

x() { echo "$1" | sed 's/^gametext x=\(-*[0-9]*\) .*/\1/'; }
x794=$(x "$p794"); xp=$(x "$p1600"); xo=$(x "$o1600")

if [ -z "$p1600" ] || [ -z "$o1600" ]; then
    say_fail "follows: the 1600 arms drew no caption at all"
else
    # The view is 1600 at a 1600x550 window (the height binds at 1:1), so the caption should
    # move right by exactly the extra width. Stated as the DIFFERENCE rather than as an
    # absolute, so this does not have to re-derive the string's length.
    want=$((x794 + 1600 - 794))
    if [ "$xp" = "$want" ]; then
        say_ok "follows: the port's caption moved from x $x794 to x $xp, the view's right edge"
    else
        say_fail "follows: the port's caption is at x $xp at a 1600 view; anchored to that view's"
        say_fail "         right edge it belongs at $want (it is at $x794 in the game's 794)"
    fi
    # THE CONTROL. Without this, "the x changed" is not evidence that the PORT changed it.
    if [ "$xo" = "$x794" ]; then
        say_ok "control: the recompiled body leaves it at x $xo in the wider view, which is"
        say_ok "         the bug this replaces"
    else
        say_fail "control: the recompiled body's caption ALSO moved (x $x794 -> $xo), so the"
        say_fail "         port's move above is not attributable to the port"
    fi
fi

[ "$fail" = 0 ] && echo "caption: ok" || echo "caption: FAILED"
exit "$fail"
