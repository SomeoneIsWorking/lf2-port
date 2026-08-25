#!/bin/sh
# Build if needed, then run the port from the game tree (the game opens its
# data with relative paths, so the working directory must be game/). Extra
# arguments are passed through to lf2; environment switches are listed in
# docs/running.md.
#
# This shim stays one line on purpose: provisioning -- shared port-assets,
# the RmlUi and Lucent submodules, the uv environment, installer extraction, the build --
# lives in bootstrap.py, where it can refuse by name and be tested.
#
#   ./run.sh                 extract + build + run
#   REBUILD=1 ./run.sh       force a rebuild
cd "$(dirname "$0")" || exit 2
exec uv run --frozen python bootstrap.py "$@"
