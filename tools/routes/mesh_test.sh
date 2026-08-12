#!/bin/sh
# The depth-tested geometry pass really tests depth (issues #49, #62).
#
# WHY THIS NEEDS A TEST AT ALL, given the pass draws nothing yet: a depth pass that is NOT
# testing looks perfectly fine on any single frame of a convex model, because the far faces are
# simply painted over by the near ones when they happen to be submitted in that order. The
# failure only shows on geometry submitted in the WRONG order -- and by the time a stage is
# authored, "the set looks a bit odd from the left" is a very expensive way to find out that
# the pipeline flag was never set.
#
# So the pass ships with a self-test that submits exactly that case -- a near triangle FIRST
# and a far one over it -- and this route is what runs it. A self-test nobody runs is the same
# bug one level up.
#
# BOTH CLASSES ARE IN THE SELF-TEST ITSELF, which is why two lines are asserted and not one:
#   the overlap pixel   must be the NEAR triangle's colour -- the depth test worked
#   the far-only pixel  must be the FAR triangle's colour  -- so the pass drew SOMETHING.
# Without the second, a pass that produced an empty target would report the same "the near one
# survived", because the near one's absence is indistinguishable from its survival at a pixel
# the far one also failed to cover.
#
# The discriminator has been run against both classes: with
# `pi.depth_stencil_state.enable_depth_test` set to false the overlap pixel comes back as the
# FAR colour and the self-test prints FAIL. That is recorded here rather than only in a commit
# message, because the next person to change the pipeline needs to know how to break it on
# purpose.
#
# GPU. This is the only route that exists to exercise a GPU pipeline directly; it runs a
# 30-frame instance and asks nothing of the game. See issue #40 for why GPU runs are counted.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

echo "mesh: the depth-tested geometry pass..."
( cd "$GAME" && \
  env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
      LF2_MESH_SELFTEST=1 LF2_QUIT_AFTER=30 \
      timeout -k 5 120 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

ready=$(grep -m1 "^mesh: depth-tested geometry pass ready" "$LOG" || true)
if [ -z "$ready" ]; then
    # NOT a pass, and not silently a skip either: the pass says WHY it could not start, and
    # that line is the useful half of this run.
    why=$(grep -m1 "^mesh: " "$LOG" || echo "(the pass printed nothing at all)")
    echo "  FAIL  the geometry pass did not come up, so its depth test was NOT exercised"
    echo "        $why"
    exit 1
fi
say_ok "ready: $ready"

near=$(grep -m1 "^mesh selftest: overlap pixel" "$LOG" || true)
far=$(grep -m1 "^mesh selftest: far-only pixel" "$LOG" || true)
verdict=$(grep -m1 "^mesh selftest: \(PASS\|FAIL\)" "$LOG" || true)

if [ -z "$near" ] || [ -z "$far" ] || [ -z "$verdict" ]; then
    say_fail "the self-test did not report all three of its lines, so this run measured"
    say_fail "nothing -- LF2_MESH_SELFTEST was set but the test did not complete"
    grep -m5 "^mesh" "$LOG" || true
else
    case "$near" in
    *"SURVIVED, so the depth test is running"*)
        say_ok "depth: $near" ;;
    *) say_fail "depth: $near"
       say_fail "       the near triangle was submitted FIRST and a far one drawn over it, so"
       say_fail "       being covered means the pipeline is not depth-testing" ;;
    esac
    case "$far" in
    *"DID draw"*) say_ok "drew: $far" ;;
    *) say_fail "drew: $far"
       say_fail "      a pass that drew nothing would report the near triangle as surviving"
       say_fail "      too, so the line above cannot be trusted without this one" ;;
    esac
    case "$verdict" in
    *PASS) say_ok "verdict: the pass's own self-test says PASS" ;;
    *)     say_fail "verdict: $verdict" ;;
    esac
fi

[ "$fail" = 0 ] && echo "mesh: ok" || echo "mesh: FAILED"
exit "$fail"
