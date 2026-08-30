---
id: 118
title: RelWithDebInfo disables assertion-based tests
status: resolved
symptom: Ubuntu 22.04 release compilation emits unused-variable warnings in assertion-only tests because CMake adds NDEBUG, so those tests report pass without executing their assertions.
tags: release,tests
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The project builds every target as RelWithDebInfo. CMake defines `NDEBUG` for that configuration,
and the test executables were not given a test-specific assertion policy. Their assert expressions
were compiled out, producing both warnings and false-green tests.

## Required fix

Keep assertions enabled for every `test_` target through one CMake-owned rule, prove the release
build is warning-free, and rerun the suite with the assertions active.

### Resolution (2026-08-30)
Applied one CMake rule that undefines NDEBUG for every test_ target without changing product release semantics. The Ubuntu 22.04 Clang 14 rebuild emitted no assertion-only unused-variable warnings, and all compiled tests passed with assertions active.
