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

## Where the bad value comes from

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
