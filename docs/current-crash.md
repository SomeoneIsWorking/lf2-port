# The startup crash — what is established

The port boots natively, completes CRT and game initialisation, loads all 690 asset files
and the 64 embedded bitmap resources, creates 39 DirectDraw surfaces, and performs three
`Blt` calls that match a real run exactly. It then faults before the first full frame.

This file records what has been **proven** about that fault, and what has been **ruled
out**, so neither gets re-derived.

## The fault

An indirect call to address 0, from guest `0043f2f4`. The chain:

```
MOV EAX, [ESP+0xec]     ; destination surface  -> 0x6f666e69
MOV ECX, [EAX]          ; its vtable           -> 0 (unmapped memory reads as zero)
MOV EAX, [ECX+0x14]     ; vtable slot 5 = Blt  -> 0
CALL EAX                                        -> call to 0
```

`0x6f666e69` is the ASCII bytes `info`. The *source* surface in the same call is a valid
object, so exactly one pointer is wrong.

## The pointer is not corrupted — ESP is displaced

**This supersedes the "clobbered pointer" reading below.** Watching the actual stack slot
settles it:

- `fn_004246b0` is entered with parameter 0 = `30000390`, the correct surface.
- The prologue stores it at `[ESP+0x20]`, which resolves to `002ff9e8`.
- That address is written **once** and **never changes** for the rest of the run.
- The bad `0x6f666e69` bytes live at `002ff7ac` and below — *lower* addresses.

So the read at `004274da` is not reading a corrupted value; it is reading the **wrong
address**, roughly `0x218` bytes below where the compiler placed the slot. ESP is
displaced inside the function.

Two things follow, and they explain why this hid for so long:

1. The stack-balance assertion cannot catch it. It fires at a function's `RET`, and this
   function never reaches its `RET` — it faults first.
2. Host handlers are not covered by that assertion at all. Only guest functions carry it,
   so an import or COM method popping the wrong number of stdcall arguments displaces the
   caller's ESP silently.

### Host-handler arity was the obvious suspect, and it is not the cause

Measured rather than assumed: ESP was logged either side of all 533 host calls made
during this function's execution (`LF2_FN_WATCH=4246b0 LF2_ESP_LOG=1`). Every delta is
correct.

| Delta | Meaning | Examples |
|---|---|---|
| `+48` | 11 args | `StretchBlt` |
| `+28` | 6 slots | `Blt` (5 + `this`), `LoadImageA` |
| `+24` | 5 args | `TextOutA` |
| `+16` | 3 slots | `GetObjectA`, `SetColorKey` |
| `+12` | 2 slots | `GetDC`, `ReleaseDC`, `SelectObject`, `SetBkColor` |
| `+8` | 1 slot | `lstrlenA`, `EnterCriticalSection`, `Restore` |
| `+4` | cdecl, caller pops | `memset`, `operator new`, `fscanf`, `feof`, `fopen`, `fgets` |

Each `+4` was checked against the import table to confirm it really is a cdecl CRT
function rather than a stdcall one mislabelled.

So no host handler displaces ESP here. Whatever moves it is in the recompiled code
itself, or the displacement reading is wrong and the read at `004274da` belongs to a
different frame than the prologue store.

Next: confirm the displacement directly by logging ESP at both `004246fd` and `004274da`
in the generated code, rather than inferring it from addresses.

## Where the bad value comes from (superseded — see above)

Established by following the value, not by inspection — every frame offset derived by hand
during this investigation turned out wrong.

```
Blt destination
  <- [ESP+0x20] in fn_004246b0        read at 004274da
  <- EDI                              stored at 004246fd, the ONLY write to that slot
  <- parameter 0                      loaded at 004246eb from [ESP+0x428]
  <- the caller
```

`[ESP+0x428]` is parameter 0, not a local: the prologue pushes three values, does
`SUB ESP,0x404`, then pushes five more — `0x424` in total — so `[ESP+0x424]` is the return
address.

`fn_004246b0` does reference the surface global (17 times) but **not on this path**. The
destination is passed in, so the wrong value originates one level up, in the caller.

## Why the first three blits work

They come from a different path. `fn_00415160` reads the surface global directly at
`00415188`. The oracle shows the destination surface is the *same object* for every blit;
the first three are colour fills (`DDBLT_COLORFILL`) and the fourth is the first keyed
sprite blit (`DDBLT_KEYSRC`). The port loses the pointer precisely at that transition.

## The correct value exists and is intact

Scanning the image data at the moment of the fault:

| Address | Value | |
|---|---|---|
| `00455608` | `30000390` | **the destination the successful blits used** |
| `00457578` | `30000370` | the `IDirectDraw` object |
| `00457584` | `300003a0` | the clipper |
| `0045560c`–`0045561c` | `30000420`–`30000460` | more surfaces |

Nothing was lost or corrupted, and no object failed to be created. The keyed-blit path
simply fails to propagate a pointer the game already holds.

## Ruled out — do not re-test

Each of these was checked and is **not** the cause:

- **ESP corruption** — a guard asserts ESP stays inside the stack on every dispatch; never fires.
- **Import-table corruption** — a watchpoint shows the relevant IAT slot holds its sentinel
  for the whole run and is never written.
- **Wrong flags** — 23,460 checks against the host CPU, exact.
- **Stack imbalance** — asserted at every guest `RET`; only `__SEH_prolog4` and its epilogue
  helpers fail, and those rewrite the stack by design.
- **Wrong import arities** — cross-checked against the game's own call sites.
- **Instruction semantics** — 8373 encodings, 65,008 checks against the host, including
  memory addressing and string operations.
- **Merged Ghidra functions** — investigated and false; the disassembly window had been
  truncated when that was claimed.
- **`control.txt` parsing** — the trailing marker line was reported as a trigger; that was a
  measurement artifact (`head -2` hid the crash line). The crash is identical without it.
- **CRLF text-mode translation** — a real difference and now fixed, but not the trigger.

## Next step

Identify the caller of `fn_004246b0` and what it passes as parameter 0. The value it
*should* pass is sitting at `00455608`.
