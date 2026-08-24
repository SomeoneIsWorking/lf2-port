---
id: 101
title: Fresh macOS setup still requires manual dependency clones
status: resolved
symptom: Running the documented out-of-box path on macOS did not provision every required checkout; the user had to manually clone dependencies before the project would build or run.
tags: reported,macos,bootstrap,provisioning,dependencies
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

There were two causes. The macOS section of `docs/running.md` and the general direct-build snippets
still told users to run `tools/build/build.py`. That tool intentionally only configures/builds; it
does not initialize `third_party/RmlUi` or clone the shared `port-assets` source. The text labelled
the same incomplete sequence a manual equivalent of bootstrap, so the documented path bypassed the
code that owned the missing checkouts. The macOS prerequisite line also named only SDL3 and CMake
even though SDL3_image, SDL3_ttf, and uv are required.

The provisioner itself also derived port-assets as `ROOT.parent.parent/shared/port-assets`, assuming
the checkout was nested exactly as `repo/pc/lf2`. A normal standalone `lf2/` checkout instead
targeted a `shared/port-assets` directory at the filesystem root, ignored the
`PORT_ASSETS_DIR`/`SHARED_DIR` precedence used by CMake, and accepted any directory containing
`sets/` even when the two required SVG inputs were absent.

## What was tried / dead ends

Static inspection found only two external source checkouts: the RmlUi git submodule and
`port-assets`; CMake has no other fetched source dependency. `git ls-remote` confirmed the default
port-assets URL exists. Isolated stdlib tests exercise both absent-checkout paths without changing
the real checkout. They also caught one bootstrap error-path gap: `ensure_port_assets` invoked a
bare `git clone` with `check=True`, producing a Python exception instead of the promised named
refusal when git or the clone failed.

## Resolution

All first-run documentation now points to `./run.sh`; direct build commands are explicitly
post-provision only. The macOS prerequisites name the required Homebrew formulae. Bootstrap now
validates explicit paths without mutating them, reuses or provisions a shared-root
checkout, reuses the established shared checkout only when complete, and otherwise clones known
port-assets revision `330c1bf` into gitignored `scratch/deps`. It validates the actual
keyboard/gamepad SVG inputs and passes the resolved path to CMake rather than asking CMake to infer
the checkout layout. It also checks for git and turns a failed clone into a checkout-specific
refusal. The offline `bootstrap` test covers precedence, the pinned clone, required files, RmlUi
initialization, missing git, clone failure, build environment handoff, and avoiding installer lookup
when the game tree already exists, and is registered in the normal CTest suite.
