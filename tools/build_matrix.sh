#!/bin/sh
# Build and test under every C compiler available, not just the default one.
#
# This is not housekeeping. The first clang build failed 40 checks in the instruction
# differential that gcc passed, because the lifter emitted `FST(i) = fpu_pop();` -- a read
# and a modification of the FPU top pointer with nothing sequencing them. Undefined
# behaviour, so gcc's ordering being correct was luck. 66,984 passing checks under one
# compiler had said nothing about it for the life of the project.
#
# macOS means clang, so this also stands in for the Mac build nobody here can run.
#
# Usage: tools/build_matrix.sh [ctest args...]      e.g. -LE slow, to skip the ~130 s pair
set -eu

fail=0
ran=0
for cc in gcc clang; do
    command -v "$cc" >/dev/null 2>&1 || { echo "== $cc: not installed, skipping"; continue; }
    ran=$((ran + 1))
    dir="scratch/build-matrix-$cc"

    echo "== $cc: configuring in $dir"
    CC=$cc cmake -S . -B "$dir" >/dev/null

    echo "== $cc: building"
    if ! CC=$cc cmake --build "$dir" -j 2>&1 | grep -E "warning|error"; then
        :        # grep found nothing, which is what a clean build looks like
    fi
    CC=$cc cmake --build "$dir" -j >/dev/null

    echo "== $cc: testing"
    if BUILD="$PWD/$dir" ctest --test-dir "$dir" --output-on-failure "$@"; then
        echo "== $cc: PASS"
    else
        echo "== $cc: FAIL"
        fail=1
    fi
done

# A matrix that silently tested one compiler is a matrix that is not doing its job, so say
# how many actually ran rather than reporting a pass that covered less than it looks like.
if [ "$ran" -lt 2 ]; then
    echo "WARNING: only $ran compiler(s) available -- this run did NOT cross-check."
fi
exit "$fail"
