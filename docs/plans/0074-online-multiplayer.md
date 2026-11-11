---
id: 0074
title: Online multiplayer by ZeroTier code over the shared rollback engine
status: open
state_items: [S014, S022, S023]
created: 2026-11-11
updated: 2026-11-11
---

# Online multiplayer

The user, 2026-11-11: "how can we integrate zerotier support for my multiplayer games ... OpenSU, Little
Fighter, X-Men, whatever else that is multiplayer capable", and "last time I tried to use zero tier, I
couldn't set it up ... maybe you can do something where the setup is handled automatically". And:
"Lf2 would probably need a new rollback netcode".

## Decided

- **Netcode:** new rollback netcode, not LF2's original network play. The rollback engine is one
  title-neutral Lucent component (input exchange, prediction, rewind and resimulate, input delay, desync
  detection) that psxport builds and proves first with Crash Bash; GGPO (MIT) and its forks are its prior
  art. LF2 writes no engine of its own and supplies four hooks: save the guest state, load it, advance one
  frame, set each player's input.
- **Transport:** an embedded ZeroTier node, the optional Lucent component the xmen2 agent is building on
  libzt (Apache 2.0 since its BSL change date, 2026-01-01). The rollback engine's traffic goes over it.
  LAN play (S014) uses the same engine over plain UDP on every interface.
- **Players** install nothing: no ZeroTier app, account, root or VPN permission, on desktop or Android.
- **Joining:** a controller-less ad-hoc network (`ff` + port range + `000000`). A player's code is their
  10-hex-digit node ID, addressable offline; ZeroTier's roots relay when no direct path exists.
- **Limits:** ad-hoc networks are IPv6 unicast only, with no broadcast or multicast, and public. Sessions
  find each other by code and authenticate the peer themselves.
- **Not in the browser build**, which has no UDP.
- psxport's `docs/plans/online-netplay.md` is the sibling design.

## Where LF2 stands

- S005: the JIT product reaches the mode menu; representative gameplay through it is not re-established.
  Nothing below can be proven in a match until it is, so each hook is proven first on the screens S005
  reaches.
- `runtime/netplay/` has an LF2-shaped hello/input packet and an IPv4 explicit-address UDP socket, each
  with a unit test and nothing in the running product. Both are superseded by the Lucent engine and
  transports and are deleted when LF2 consumes them.
- LF2's own network and recording modes (`0x00450b80`, `0x0044f1af`) drive the game from per-slot
  seven-bit masks alone. That is evidence the engine is deterministic given its inputs and seed; the new
  netcode does not use that protocol.

## The four hooks and what each needs

### Advance one frame: deterministic stepping

The same image, start state and per-frame inputs give the same guest state on every peer.

- **Clock.** Guest time is `presented frames * GUEST_FRAME_NS + guest_clock_offset_ns`
  (`runtime/win32/imports.c`), never the wall. Two gaps: it counts *presented* frames, so a resimulated
  frame that is not presented does not advance it; and the offset is host state outside guest memory.
  Guest time moves to a logic-frame counter that the snapshot owns.
- **Random numbers.** `h_rand`/`h_srand` call the host libc, whose generator differs between glibc,
  bionic and libSystem and whose state no snapshot sees. They become MSVCR80's generator
  (`holdrand = holdrand * 214013 + 2531011; return (holdrand >> 16) & 0x7fff`) with snapshot-owned state.
  That is also a fidelity fix: the game's recordings depend on it.
- **Seed.** `_time64` reads the wall clock. The session's start state fixes the seed.
- **Host-varying imports.** Every HLE return that can differ between machines is either routed through
  the session's inputs or shown not to reach logic: keyboard messages and `GetAsyncKeyState` (LF2's
  function keys pause, restart and change speed), cursor and mouse, focus and activation, DirectSound
  play positions, file timestamps.
- **Native overrides with state.** `runtime/overrides/input.c` (device claims, `dev_prev`,
  `mouse_confirm_frames`) and `coop.c` (drop-in, late character selection) decide from local devices. In
  a session they decide from the session's inputs only, and their state is in the snapshot.
- **x87 and SSE across backends.** LF2's object positions are floating point. A desktop x86-64 peer and
  an Android ARM64 peer agree only if x86port's backends give bit-identical x87 results; that is
  x86port's contract, checked here by cross-host state hashes.
- **Suppressed frames.** A resimulated frame runs the guest frame with presentation and audio
  suppressed and still advances the logic clock.

### Set each player's input: a fixed frame boundary

`fn_00419a60` (native override, `runtime/overrides/input.c`) is the one per-frame gather that turns
devices into each slot's seven buttons. It is the boundary: local devices produce the local player's
input, which goes to the engine, and the gather writes every slot from the engine's frame inputs. Local
device sampling and slot writing split into two owners there so a session can replace the source.

### Save and load: a complete, fast snapshot

A snapshot holds everything the next frame reads:

- every writable guest region (PE data, heap and arenas, stack, TIB, COM objects; surfaces only if logic
  reads them, which is to be shown, not assumed), saved as the pages dirtied since a base through the
  guest-memory prewrite notification `guest_memory` already raises;
- CPU state at the boundary;
- host state the guest reads: heap allocator metadata, the logic-frame clock, the generator, native
  override state, timers, and DirectSound buffer state and play position.

Save and load happen at the gather boundary, reached from the same guest call chain every frame. Load
refuses unless guest `ESP` and the executor's interception depth equal the snapshot's. No code page is
restored, so translated code stays valid. Cost is measured in bytes and pages per frame and must leave
room for one save plus several resimulated frames inside a 33 ms frame.

### Hash: desync detection

The engine compares per-frame hashes; LF2's hash covers the snapshot's contents. The first mismatching
frame is reported with both states saved and traced to its owner.

## Session around the engine

- **Authentication.** ZeroTier authenticates node identities, so a datagram's source node ID is the
  peer's. On the public ad-hoc network the host admits only codes its player accepted, and the hello
  refuses a peer whose executable SHA-256 differs. LAN applies the same admission to addresses.
- **Player flow.** The port menu gains Online: *your code* (the node ID, from the node identity persisted
  in OS user data), a field for a friend's code, and a LAN list. The ZeroTier node starts when the player
  opens Online; no setting or environment variable gates it.

## Phases

Each phase is proven in the running product with two instances, each in its own rootless network
namespace (`unshare -rn` with a veth pair for LAN; ZeroTier through the real roots for internet).

1. **Determinism on reachable screens.** MSVCR80 generator, logic-frame clock, snapshot-owned host state.
   A per-frame state hash at the gather; two processes from boot with the same inputs log identical
   hashes through every frame S005 reaches.
2. **Save and load** at the gather boundary: save, step, load, step again gives identical hashes; cost
   measured.
3. **Input hook:** local sampling split from slot writing; a scripted input source replaces devices and
   two processes stay hash-identical.
4. **LAN (S014):** consume the Lucent rollback engine over host UDP between two namespaces.
5. **Internet (S022):** the same over the Lucent ZeroTier component, two namespaces with no shared route,
   joined by code.
6. **Rollback in a match (S023)** once S005 reaches gameplay; then the Online menu.

## Progress

- 2026-11-11: plan written. Lucent component names and APIs requested from the xmen2 agent (ZeroTier)
  and the psxport agent (rollback engine).
