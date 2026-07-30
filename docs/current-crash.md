# Startup crash — RESOLVED

**Root cause: unreliable function-end detection.** Ghidra's declared function sizes are
not trustworthy — `fn_00423480` reports 576 bytes but its body continues past that end.
The lifter emitted only up to the declared end, so control ran off the end of the
generated C function and returned **with the frame still allocated**. The caller then
continued with ESP 0x218 low and `EBP` unrestored, and read its own local from the wrong
address. That is where the `0x6f666e69` (`"info"`) bytes came from: not a corrupted
pointer, just a read from the wrong place.

Nothing caught it because no `RET` ever executed, so the stack-balance assertion — which
fires at `RET` — stayed silent.

## The fixes

- Function ends now follow **control flow**: keep going while the code falls through, stop
  only past the declared end after a terminator with no forward branch pointing further
  on, and treat `INT3` padding as a hard end. Neither the size field nor the next entry is
  reliable (`derive_entries` can plant a synthetic entry mid-body).
- Reaching the end of a lifted function without executing a `RET` now **aborts by name**
  instead of returning silently. This guard fired on exactly `fn_00423480`, turning a wild
  call to address 0 into a named failure at the real fault.

## What it cost, and the lesson

Roughly a dozen rounds. The hypotheses that failed — ESP corruption, import-table
corruption, wrong flags, stack imbalance, import arities, instruction semantics, Ghidra
function merging, `control.txt` parsing, the /GS cookie, internal tail calls — are listed
below with their evidence, because each was ruled out by measurement and none should be
retried.

Two of those cost a round each through **my own measurement errors**: a truncated
disassembly window made a function look merged, and a `head -2` pipe hid the crash line so
a file looked like a trigger. Where a conclusion rested on a hand-derived stack offset it
was wrong at least three times. Every conclusion that came from comparing against the Wine
oracle, or from watching a real address at runtime, held up.

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

## Where the port is now

It runs its render loop. DirectDraw calls went from 167 to over 14000, of which 6580 match
the oracle in order, and the window shows the menu's blue background with the banner and
panel regions correctly placed.

Two rendering bugs were found and fixed immediately after: presentation used surface
*width* as the row stride where surfaces are dword-aligned *pitch* (794 vs 796, producing
a diagonal shear), and surfaces were 8-bit indexed on a false assumption — the game never
creates a palette, it queries `GetPixelFormat` and adapts, so surfaces are now 32-bit XRGB
with GDI converting the 8-bit bitmaps through their own palettes.

**Resolved.** The bitmaps are RLE8-compressed and the loader read them as raw rows. Their
headers declare far more pixels than the files hold, so the read produced garbage and then
ran out partway down. Implemented RLE8 for both the file and resource paths, and the menu
renders.

Two claims made along the way were wrong, both from bad measurement rather than bad
reasoning about the code:

- "only one StretchBlt happens" — the counter logged every 200th call, so `#1` meant the
  first, not the only. There are 26, one per sheet.
- "the default SDL driver presents nothing" — SDL selects its Wayland backend whenever a
  compositor is reachable, so the window was opening on the real desktop while the capture
  photographed an empty Xvfb. Not a port defect at all. See `docs/running.md`.

## Still open

- ~~Some background regions render black; the sheets themselves all decode correctly, so
  this is compositing rather than loading.~~ **Fixed**, and the diagnosis above was right
  that it was compositing: the cause was ADC/SBB dropping the carry, which made the game
  compute `DDBLT_KEYSRC` as 0, so every colour-keyed blit drew opaque. See
  `docs/codemap.md`. The same one-line class of bug accounted for the black slab behind
  the main menu, the black boxes around fighter sprites, and the missing ad artwork.
- GDI `TextOutA` draws nothing, so text drawn through GDI is missing. The menu's own text
  is bitmap art and does appear.
