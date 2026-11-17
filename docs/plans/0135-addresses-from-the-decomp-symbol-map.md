# 0135 — Guest addresses from the decomp's symbol map

## Goal

The port reads every guest address, structure size and field offset from constants generated
from `external/lf2-decomp/config/symbols/lf2.txt`, never a hand copy. A finding is committed to
the decomp first; the port bumps the submodule in the change that uses it.

## Where it stands

- The map holds every name the port gives an address (104 symbols: the 23 native overrides,
  `Sprite_DrawFill`, `CharMenu_Flow`, the IAT slots, strings and `.data` words). Addresses the port
  names twice are listed in the decomp's `docs/our-names.md`.
- x86port's `tools/symbol_constants.py` accepts the map (format, duplicates, sections, code in
  `.text`), but it writes only a C++ header (`inline constexpr` in a namespace). Every LF2 owner
  that names an address is C, so stage 1 waits on a C output from that generator.

## Stages

1. **Generator.** x86port's `symbol_constants.py` and `x86port_symbol_constants()` gain a C
   output: one header of `#define <PREFIX><Name> 0x...u` (and `<PREFIX><Name>_size`), usable in
   `enum` initialisers and `case` labels, from the same checks. LF2 calls it with prefix `LF2_`
   into `build/<tree>/lf2_symbols/include/`.
2. **Named addresses.** Replace the hand-written enums with the generated names, owner by owner,
   highest count first: `runtime/overrides/startup_frontend.c`, `runtime/app/port_entry.c`,
   `runtime/overrides/startup_world.c`, `text.c`, `menu.c`, `world.h`, `objects.c`,
   `boot_guest.c`, `background.c`, `cheats.c`, `runtime/app/main_loop.c`, `screens.c`,
   `object_parser.c`, then the rest. `native_override.c` registers each override by its generated
   function constant. Each step resolves its `our-names.md` entry to the map's name and is proven
   by identical object code for the touched files, then a `tools/routes/jit_scan.py` run.
3. **Unnamed literals.** Name each remaining address literal (330 distinct in `runtime/` today)
   in the decomp, then use the constant.
4. **Layouts.** Move structure sizes and field offsets (world `0x00458B00` fields, the object
   stride `0x420`) into the decomp, then drop the port's hand-written layouts.

## Done when

A search of `runtime/` finds no guest address or field offset literal outside generated code, and
the state scan still reaches the same screens with the same JIT stats.

A new address is named in the symbol map first and used through the generated constant.
