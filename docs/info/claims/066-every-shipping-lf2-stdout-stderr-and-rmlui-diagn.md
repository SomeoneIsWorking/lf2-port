---
id: C066
kind: claim
status: holds
created: 2026-08-25
tags: logging,timestamps
depends: runtime/log/lf2_log.cpp#append_log, runtime/ui/rmlui_system.cpp#LogMessage
---

## Claim

Every shipping LF2 stdout/stderr and RmlUi diagnostic is emitted as a complete Lucent record with a millisecond UTC timestamp

## Evidence

The logging unit tests cover fragmented stdio, empty delimiters, multi-line RmlUi-style messages, severity, timestamp shape, and unchanged file output; all 39 Clang CTests passed except the external-tool shader skip; a real smoke route required the timestamped native-entry record; a zero-argument launcher run showed timestamped first-party and RmlUi font records.

## What would falsify it

Any shipping stdout/stderr or RmlUi diagnostic appears without the ISO 8601 prefix, splits one logical line into partial records, or bypasses lucent::log.
