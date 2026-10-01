---
id: 118
title: RelWithDebInfo disables assertion-based tests
status: closed
symptom: Ubuntu 22.04 release compilation emits unused-variable warnings in assertion-only tests because CMake adds NDEBUG, so those tests report pass without executing their assertions.
tags: release,tests
created: 2026-08-30
updated: 2026-09-09
---

## Root cause
The project builds every target as RelWithDebInfo. CMake defines `NDEBUG` for that configuration,
and the test executables were not given a test-specific assertion policy. Their assert expressions
were compiled out, producing both warnings and false-green tests.
