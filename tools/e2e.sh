#!/bin/sh
# The end-to-end scripts: boot the game, drive it through its menus, assert what happened.
#
# THESE ARE NOT ctest TESTS AND DELIBERATELY SO. `ctest` is one suite and it finishes in about
# a second and a half; every script below boots a full game instance and drives it for
# thousands of frames, so the quickest is seconds and the thorough ones are minutes. Mixing
# the two put a five-minute wall in front of the thing you run after every edit, and the
# predictable happened -- the fast checks stopped being run because the slow ones were bundled
# with them, and the mouse route sat green and broken for as long as it existed.
#
# So the split is by WHAT THE CHECK NEEDS, not by a label:
#
#   `ctest`          arithmetic and data. The decoder against Ghidra's disassembly, the
#                    instruction differential, the blitter, the mixer, the flags, the shader
#                    blobs, and runtime/overrides/geom.h's geometry. Under two seconds, run it
#                    constantly, and it is allowed to be the reason you believe something.
#
#   `tools/e2e.sh`   the questions that genuinely need a running game: does a route REACH a
#                    screen, does a second pad drive its fighter, does the GPU renderer draw
#                    what the software one draws. Nothing offline can answer those.
#
# A claim that COULD be checked offline belongs in geom.h and runtime/test_geom.c, not here.
# The audio pan is the worked example: a three-run, 270-second script became 20 assertions in
# a millisecond, and gained a walk across every on-screen pixel the script had never done.
#
# RUN THEM ONE AT A TIME. Each wraps its instance in a wall-clock `timeout`, and two instances
# on one machine take long enough to trip it -- concurrency here produces failures from
# scripts that pass individually. That is why this loops rather than backgrounding.
#
# Unplug any physical controller first: an attached pad binds gamepad slot 0 and silently
# stalls every scripted route at the front end.
#
#     tools/e2e.sh              # all of them, in this order
#     tools/e2e.sh mouse render # only the ones whose name matches
#     BUILD=... GAME=... tools/e2e.sh
set -eu

cd "$(dirname "$0")/.."
BUILD=${BUILD:-scratch/build}
GAME=${GAME:-game}
export BUILD GAME

# Ordered cheapest-first, so a broken build fails in seconds rather than after the renderer
# comparison. One line each on what only a running game can say:
ALL="
smoke             the port boots, loads its data and reaches a match
controller        a pad alone drives the game -- the mouse and key routes cannot see this path
mouse             the pointer alone reaches charselect, the overlay and a match (issue #26)
controller_2p     a second pad joins as player two, with a one-pad run as the control
coop_dropin       a pad joins a match ALREADY RUNNING and drives the fighter it joined
coop_select       a late joiner picks its character from the game's own roster
pause_dropout     a joined player leaves from the pause menu, on its own pad
two_human_match   pad two drives its fighter in the FIGHT, not just at selection
widescreen        the composition follows the window, both directions, natives as negatives
resize            a resize leaves no stale pixels standing (issue #29)
background        the background override draws what the recompiled body drew, byte for byte
stage_mode        the port reaches STAGE mode and the section lock holds the camera (issue #36)
render            the GPU renderer draws what the software compositor draws (issue #30)
"

pass=0; fail=0; skip=0; failed=""
ran=0

for name in $(echo "$ALL" | awk 'NF {print $1}'); do
    if [ "$#" -gt 0 ]; then
        want=0
        for arg in "$@"; do [ "$arg" = "$name" ] && want=1; done
        [ "$want" = 1 ] || continue
    fi
    script="tools/${name}_test.sh"
    if [ ! -f "$script" ]; then
        echo "MISSING  $name: $script does not exist"
        fail=$((fail + 1)); failed="$failed $name"
        continue
    fi
    ran=$((ran + 1))
    echo
    echo "=== $name ==================================================="
    set +e
    sh "$script"
    rc=$?
    set -e
    case $rc in
    0)  pass=$((pass + 1)) ;;
    77) skip=$((skip + 1)); echo "  SKIPPED (exit 77) -- this is NOT a pass" ;;
    *)  fail=$((fail + 1)); failed="$failed $name" ;;
    esac
done

echo
# A run that matched nothing must say so rather than printing a clean summary of no work.
if [ "$ran" = 0 ]; then
    echo "e2e: NOTHING RAN. No script matched${*:+ }${*:-}, and the names are:"
    echo "$ALL" | awk 'NF {print "    " $1}'
    exit 2
fi
echo "e2e: $ran script(s) -- $pass passed, $fail failed, $skip skipped"
[ "$skip" = 0 ] || echo "     a skip means the game tree or the build was missing; it proves nothing"
if [ "$fail" != 0 ]; then
    echo "     failed:$failed"
    exit 1
fi
exit 0
