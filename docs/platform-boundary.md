# Platform boundary — traced call surface

Ground truth captured by running the real `lf2.exe` under Wine and enabling Wine's internal
debug channels. Traces in `scratch/logs/` (gitignored; regenerate with the commands below).

```sh
# DirectDraw + DirectSound COM method surface
WINEDEBUG=+ddraw,+dsound wine lf2.exe
# legacy joystick surface
WINEDEBUG=+winmm,+mmsys,+joystick,+dinput wine lf2.exe
```

## ⚠ Instrument warning: Wine relay tracing is USELESS here

`WINEDEBUG=+relay` with `RelayInclude` produces **zero** function-level lines for this
32-bit PE under wow64 wine-staging 11.0 — validated by pointing it at `ddraw.dll` and
getting 0 hits for `DirectDrawCreate`, a call the `+ddraw` channel proves happens.

Its failure mode is a silent empty result, which reads identically to "the game never calls
this". Do not conclude anything from a quiet relay log. **Use debug channels instead.**

## Video — DirectDraw 1 interfaces

The game creates ~28 surfaces and drives them through the *DirectDraw 1* vtables
(`ddraw_surface1_*`, `ddraw1_*`), not DD7. Per 20 s at the main menu:

| Call | Count | Meaning for the port |
|---|---|---|
| `ddraw_surface1_Blt` | 8670 | sprite blitting — the hot path |
| `ddraw_surface{1,7}_GetDC` / `ReleaseDC` | 1651 pairs each | **the game draws with GDI onto DD surfaces** (this is the `TextOutA` text path) |
| `ddraw_clipper_GetClipList` | 1084 | windowed-mode clipping |
| `ddraw_surface1_SetColorKey` | 26 | colour-key sprite transparency |
| `ddraw1_CreateSurface` | 28 | small, fixed surface set |
| `ddraw1_SetCooperativeLevel`, `CreateClipper`, `SetClipper`, `Restore`, `GetSurfaceDesc`, `GetPixelFormat` | 1–26 | init / mode plumbing |

The `GetDC`/`ReleaseDC` pairing is the notable one: a straight "DirectDraw → SDL texture"
port misses it, because part of every frame is drawn by **GDI**, not DirectDraw. The runtime
needs a GDI-on-surface story (`TextOutA`, `SetTextColor`, `SetBkColor`, `StretchBlt`).

## Audio — DirectSound

Game-facing calls only (the `DSOUND_Mix*` / `mixieee32` entries in the trace are Wine's own
mixer internals, not the game):

`DirectSoundCreate` → `SetCooperativeLevel` → `CreateSoundBuffer` (41) →
per-sound `Lock` (9744) / `Unlock` (3249) / `GetCurrentPosition` (6771) / `Play` /
`SetVolume` / `SetPan`.

The game writes PCM into secondary buffers by hand. Straightforward to host on SDL3 audio.

## Input — legacy joystick, and why hotswap doesn't work

Full observed sequence, **once at startup**, for `id 0` and `id 1` only:

```
joyGetPosEx id N  →  joySetThreshold id N, threshold 100
                  →  joySetCapture hwnd ..., id N, period 25, changed 1
                  →  joyGetPos id N  →  joyGetPosEx id N
                  →  joyGetDevCapsA id N  →  joyGetDevCapsW id N
```

Three independent reasons hotplug cannot work in the stock game:

1. **Enumeration happens once, at startup.** A pad plugged in later is never looked for.
2. **Only ids 0 and 1 are ever probed** — hardcoded to LF2's two-gamepad maximum. These are
   fixed legacy driver slots, not stable per-device identities.
3. **`joySetCapture` (period 25 ms) asks Windows to post `MM_JOY*` messages to the window**
   rather than polling. That mechanism has no device-arrival notification at all.

So auto-detect/hotswap is not a bug to patch — the API the game chose has no concept of it.
The fix is to replace this surface with SDL3's gamepad layer, which has device-added/removed
events, and to re-enumerate on those events instead of once at startup.

### Open question — do NOT assume settled

This trace was captured with **no controller attached** (`find_joysticks found 0 device
instances`), so we observed the *failure* path. It is therefore **unverified** whether the
game polls `joyGetPosEx` per-frame when a device *is* present, or whether it stopped because
caps queries failed.

That distinction matters: if the game does poll per-frame, a runtime that re-enumerates on
hotplug may be enough; if it caches capabilities at startup, we also have to invalidate that
cache. **Re-run this trace with a real pad attached before designing the input override.**
