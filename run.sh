#!/bin/sh
# Build if needed, then run the port from the game tree (the game opens its
# data with relative paths, so the working directory must be game/).
# Extra arguments are passed through to lf2; environment switches are listed
# in docs/running.md.
set -e
cd "$(dirname "$0")"

# The game tree is not committed; it is extracted from the installer's overlay
# on first run (tools/extract_game.py, no Windows or Wine involved). The build
# needs it too -- the recompiler reads game/lf2.exe.
if [ ! -f game/lf2.exe ]; then
    python3 tools/extract_game.py LF2_v2.0a.exe game
fi

python3 tools/build/build.py

cd game
exec ../scratch/build-clang/lf2 lf2.exe "$@"
