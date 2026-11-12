---
id: 0129
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
"Lf2 would probably need a new rollback netcode". And: "Firebase for you know a fixed discovery point
online", and "Make the framework capable of multiple solutions imo, Incase firebase doesn't work out, but
all solutions should require no extra install like a VPN client like tailscale".

## Decided

- **Netcode:** new rollback netcode, not LF2's original network play. The rollback engine is one
  title-neutral Lucent component (input exchange, prediction, rewind and resimulate, input delay, desync
  detection) that the lucent-online session builds, proven first with psxport's Crash Bash; GGPO (MIT)
  and its forks are its prior art. LF2 writes no engine of its own and supplies four hooks: save the guest state, load it, advance one
  frame, set each player's input.
- **Transport:** an embedded ZeroTier node, the optional Lucent component the xmen2 agent is building on
  libzt (Apache 2.0 since its BSL change date, 2026-01-01). The rollback engine's traffic goes over it.
  LAN play (S014) uses the same engine over plain UDP on every interface.
- **Discovery:** Lucent discovery, built by the lucent-online session behind one `Directory` interface
  (`publish`, `list`, `withdraw`), several backends queried at once and merged, an unreachable one
  degrading to the others: Nostr (expiring signed events on public relays), a self-hosted lobby on
  `lucent::http::Server`, and Firebase Realtime Database. None needs an install. Play by a shared code
  (invite and reply) needs no discovery and always works. Anyone can read a listing, so security stays
  in the session layer.
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

- **Clock.** Guest time is `guest frames * GUEST_FRAME_NS + offset` (`runtime/win32/guest_clock.c`),
  never the wall. Guest frames count the frames the guest presented whether or not the host showed
  them, so a resimulated frame advances it; both words are in the snapshot.
- **Random numbers.** `h_rand`/`h_srand` call the host libc, whose generator differs between glibc,
  bionic and libSystem and whose state no snapshot sees. They become MSVCR80's generator
  (`holdrand = holdrand * 214013 + 2531011; return (holdrand >> 16) & 0x7fff`) with snapshot-owned state.
  That is also a fidelity fix: the game's recordings depend on it.
- **Seed.** `port_entry.c:initialise_window` seeds with `srand(guest_time_ms())`, the guest clock, so
  the seed is already deterministic. `_time64` (wall clock) feeds only the build and update date
  strings in `initialise_dates`; that they never reach logic is to be shown.
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

### The frame boundary

The native entry owns the main loop: `port_entry.c:run_main_loop` pumps guest messages
(`0x0043d230`) and then calls the guest frame `0x0043e9a0` from the top level, once per logic frame.
Between two calls no guest frame is on the host stack, so `advance` is one `run_game_frame` and save
and load happen between frames with the executor's interception depth at zero, asserted. The guest
message pump is a host-varying input of its own: in a session, keyboard messages that reach logic come
from the session's inputs or are withheld.

### Set each player's input

`fn_00419a60` (native override, `runtime/overrides/input.c`) is the one per-frame gather that turns
devices into each slot's seven buttons. It is the boundary: local devices produce the local player's
input, which goes to the engine, and the gather writes every slot from the engine's frame inputs. Local
device sampling and slot writing split into two owners there so a session can replace the source.

### Save and load: a complete, fast snapshot

A snapshot (`runtime/netplay/rollback_state.cpp`) holds everything the next frame reads:

- every logic span (PE data, heap, stack, TIB, COM objects) through the write journal
  (`runtime/cpu/guest_journal.c`): between saves the pages are read-only, the first write to one from
  translated or host code faults and keeps its pre-image, and a load puts the pre-images back and
  invalidates translations over them. Syscalls writing guest memory note the write through
  `guest_write_pointer` first. Surfaces and sound buffers are left out: every frame draws them again
  from logic, shown by the rerun check's media digests matching a plain run.
- CPU state at the boundary (`lf2_jit_save_cpu`);
- host state the guest reads: the clock, the generator, the mapped-span list and the heap's next
  address. Still outside it: COM host objects (a load across a frame that created one aborts), native
  override state (`coop.c`, `input.c`), DirectSound buffer state and play position, and the message
  pump between frames.

The same state owner serves issue 0127's durable snapshot (Android process recreation): one set of
title-owned sections, kept in memory per frame for rollback and serialized only for recovery.

Save and load happen at the frame boundary above and abort unless the executor is at depth zero. Cost
is counted in pages written per frame.

### Hash: desync detection

The engine compares per-frame hashes; LF2's hash covers the snapshot's contents. The first mismatching
frame is reported with both states saved and traced to its owner.

## Session around the engine

- **Authentication.** ZeroTier authenticates node identities, so a datagram's source node ID is the
  peer's. On the public ad-hoc network the host admits only codes its player accepted, and the hello
  refuses a peer whose executable SHA-256 differs. LAN applies the same admission to addresses.
- **Player flow.** The port menu gains Online: open sessions from Lucent discovery, *your code* (the
  node ID, from the node identity persisted in OS user data), a field for a friend's code, and a LAN
  list. Hosting publishes the session through the `Directory`. The ZeroTier node starts when the player
  opens Online; no setting or environment variable gates it.

## Phases

Each phase is proven in the running product with two instances, each in its own rootless network
namespace (`unshare -rn` with a veth pair for LAN; ZeroTier through the real roots for internet).

1. **Determinism on reachable screens.** MSVCR80 generator, logic-frame clock, snapshot-owned host state.
   A per-frame state hash at the frame boundary; two processes from boot with the same inputs log identical
   hashes through every frame S005 reaches.
2. **Save and load** at the frame boundary: save, step, load, step again gives identical hashes; cost
   measured.
3. **Input hook:** local sampling split from slot writing; a scripted input source replaces devices and
   two processes stay hash-identical.
4. **LAN (S014):** consume the Lucent rollback engine over host UDP between two namespaces.
5. **Internet (S022):** the same over the Lucent ZeroTier component, two namespaces with no shared route,
   joined by code.
6. **Discovery (S022):** Lucent discovery lists the host's session and the reader joins by its code.
7. **Rollback in a match (S023)** once S005 reaches gameplay; then the Online menu.

## Progress

- 2026-11-11: plan written. Provisional Lucent APIs, neither built yet: `lucent::zerotier`
  (`Node::start`, `code()`, `open_datagram(port)`, send to a code) from the xmen2 agent, and
  `lucent::rollback::Session` with a consumer `Runtime` (`save(slot)`, `load(slot)`, `setInputs`,
  `advance`, `hash`) from psxport; LF2 is one byte per slot, eight slots.
- 2026-11-11: phase 1 started. Host time reached guest state three ways, each fixed at its owner:
  `_time64`/`GetLocalTime`/`GetSystemTimeAsFileTime` read the wall (`BUILD_DATE`, `UPDATE_DATE` and a
  `struct tm` differed between runs), now the guest calendar in `runtime/win32/guest_clock.c` (an epoch
  fixed once, advanced by guest time); `lf2_loading_now` chose the Sleep credit from `CLOCK_MONOTONIC`,
  now guest time; `rand`/`srand` were host libc, now MSVCR80's generator in `crt_random.c`.
  `tools/routes/determinism_test.py`: two hidden runs of the smoke inputs give identical logic and
  media digests on every common logic frame (200, boot through character select, the S005 frontier).
  The differing run lengths seen first were the tool's own timeout cutting slow runs short; with clean
  exits required, 299 logic frames match on logic, media and presents.
  Digesting every span costs about 1.5 s a frame, so the rollback hash must cover dirty pages only.
  Time zone still reaches `localtime`; a session must agree on it.
- 2026-11-11: S014/S022 meeting. Lucent pinned at `282e224` with `lucent::online` on desktop.
  `tools/routes/online_meeting_test.py`: host and friend in separate rootless network namespaces meet
  by invite code over the direct path, both report the same `lf2.exe`, and one input frame crosses each
  way. `lan_socket.*` is deleted, superseded by Lucent's transport. Next: the ZeroTier backend
  (`lucent::zerotier`, libzt fork `6e74b65`), then the rollback hooks.
- 2026-11-11: the ZeroTier node is in. `tools/build/libzt.py` provisions the libzt fork `6e74b65`
  once per compiler into the shared Lucent prefix; desktop builds link `lucent::zerotier`, and the
  connector dials through the node beside the direct path. The direct meeting still passes with a
  node per instance. Next: a ZeroTier-only meeting through a lab root in the namespaces.
- 2026-11-11: Lucent `dc64034`. `online_meeting_test.py --zerotier`: direct path off, a dummy `lab0`
  carrying `lucent_zerotier_root`, both instances log `connected over ZeroTier`, see the same `lf2.exe`
  and trade a frame. Next: the rollback hooks on `lucent::rollback::Runtime`.
- 2026-11-11: phase 2. Save and load through the write journal; the state hash is one formula per
  4 KiB guest page, kept current from the pages each frame wrote, and the determinism digest uses it
  (300 frames now take seconds, not minutes). `tools/routes/rollback_test.py`: every frame runs unshown,
  loads back and reruns shown, with no frame differing and every logic and media digest equal to a plain
  run's over 299 frames; 11 to 12 logic pages written a frame. Restoring surfaces would have cost 4400
  pages a frame. No frame on this route allocates, so heap and mapping restores are covered by
  `test_guest_journal` only. Next: the Lucent `Runtime` over this state, then the input hook.
- 2026-11-11: phases 3 and 4. `main_loop.c` advances one logic frame; `session_inputs.h` feeds the
  gather's devices from the session's slots and makes device claims, late-join selections and the
  window message queue snapshot sections; host keys and mouse are withheld in a session.
  `game_runtime.cpp` is LF2's `lucent::rollback::Runtime`. The meeting now happens before boot; the
  hello (wire version 2) carries the executable hash and the host's calendar, which the friend adopts.
  `online_meeting_test.py`, direct and `--zerotier`: both peers log equal final hashes on every
  reported frame, no desync. Gaps: idle inputs only and no rewind yet in a two-peer run; audio during
  resimulation; frames that create COM objects cannot be loaded; the lazy guest buffer behind
  `localtime`; a session starts at boot only; no Online menu.
- 2026-11-12: real rewinds. `online_meeting_test.py` gives each player its own pad script and puts
  `netem delay 60ms 20ms loss 10%` on the cable; both peers resimulate about 100 to 200 frames with
  equal final hashes and no desync, six runs of six. A scripted pad now plays without window focus
  (SDL drops joystick input to an unfocused app, and a hidden window has none). Two instances from one
  install corrupted each other's object data through `data\temporary.txt` (issue 0130); the test gives
  each its own install. Reports are bounded by Lucent `26004e4`'s `finalized_frame()`.
- 2026-11-12: the Online menu. Lucent `4a14337`. The ONLINE page hosts, copies and pastes codes and
  lists open games; a listing carries a contact key, never the invite, and `lobby_list.cpp` answers
  each ask with the invite through sealed letters. The friend adopts the host's state through
  hello v3 and a page sync instead of both booting together. `online_meeting_test.py` drives both
  menus by pad from one install against a lab `lucent_lobby_server`, direct and `--zerotier`: equal
  final hashes, host and friend both resimulating.
- 2026-11-12: a remote press steers a fighter. After the meeting the host starts a VS match, the
  friend's pad claims player 2 in character select, and its presses run player 2 across the stage
  on both screens while the host walks player 1; equal final hashes through the match load, direct
  (host 267, friend 291 frames resimulated) and over ZeroTier. Pad items can be keyed `@online`, the
  first frame the session plays, and `@lobbies` the first frame the ONLINE page lists a game.
- 2026-11-12: rerun frames are silent and leave no draws. `gdi.c`'s handle tables join the snapshot
  (issue 0131), an unshown frame's display list is reset (issue 0133), and `dsound_set_audible`
  holds a rerun frame's Play, Stop, seek and volume calls, as `lucent::rollback::Runtime` asks.
