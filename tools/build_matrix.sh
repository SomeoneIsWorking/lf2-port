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
# Optimisation level is varied as well as compiler. Evaluation order for unsequenced
# operations is the front end's choice and can differ between -O0 and -O2 in the same
# compiler, so it widens the net for the same bug class at no extra thought.
#
# WHAT DOES NOT WORK, checked rather than assumed: neither `clang -Wunsequenced` nor
# `gcc -Wsequence-point` can see this. Fed the exact original defect --
# `FST(i) = fpu_pop();` with fpu_pop a static inline that modifies cpu.st_top -- both
# compilers are silent, while both flag a syntactic `i = i++` control in the same file.
# They only handle the syntactic cases. A clean warning sweep is NOT evidence here, which
# is why the matrix is a matrix and not a compiler flag.
#
# Usage: tools/build_matrix.sh [ctest args...]      e.g. -LE slow, to skip the ~130 s pair
set -eu

fail=0
ran=0
for cc in gcc clang; do
    command -v "$cc" >/dev/null 2>&1 || { echo "== $cc: not installed, skipping"; continue; }
    for opt in "" "-O2"; do
        ran=$((ran + 1))
        tag="$cc${opt:+$opt}"
        dir="scratch/build-matrix-$cc${opt:+-O2}"

        echo "== $tag: configuring in $dir"
        CC=$cc CFLAGS="$opt" cmake -S . -B "$dir" >/dev/null

        echo "== $tag: building"
        CC=$cc cmake --build "$dir" -j 2>&1 | grep -E "warning:|error:" || true
        CC=$cc cmake --build "$dir" -j >/dev/null

        echo "== $tag: testing"
        if BUILD="$PWD/$dir" ctest --test-dir "$dir" --output-on-failure "$@"; then
            echo "== $tag: PASS"
        else
            echo "== $tag: FAIL"
            fail=1
        fi
    done
done

# A matrix that silently tested one compiler is a matrix that is not doing its job, so say
# how many actually ran rather than reporting a pass that covered less than it looks like.
if [ "$ran" -lt 4 ]; then
    echo "WARNING: only $ran configuration(s) ran -- this was not a full cross-check."
fi
exit "$fail"
