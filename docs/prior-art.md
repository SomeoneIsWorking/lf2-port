# Prior art

External work on Little Fighter 2 v2.0a `lf2.exe` (3,424,256 bytes, CRC32 `937da83c`, SHA-256 `12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`, image base `0x00400000`), checked against `game/lf2.exe` with `objdump`.

| project | URL | kind | license | target build | build match (addresses checked) | what we may take | notes |
|---|---|---|---|---|---|---|---|
| openlf2 (xsoameix) | https://github.com/xsoameix/openlf2 | partial non-matching decomp, injected DLL replacing three functions | none (all rights reserved) | v2.0a | matched: `0x00403270` (teleport), `0x00417170` (random: globals `0x00450c34`, `0x00450bcc`, table `0x0044ff90`, moduli 1234/3000 identical), `0x00417400` (attack-success check) are function starts with the described bodies | names only | Last push 2014; three functions. Mirror at `zort/openlf2`. |
| lf2-ai-scriptengine (Silva and SomeoneElse, zort) | https://github.com/zort/lf2-ai-scriptengine | ddraw.dll wrapper with Detours hooks and AngelScript AI; `sgame.h` game-struct layout | none | v2.0a | matched: detours at `0x00403a40` and `0x004094b0` are function starts; `game` = `0x00458b00`, objects `0x00458c94`, mode `0x00451160`, elapsed `0x00450b8c` agree with our overrides and issue 0051 | names only (struct field names, global names) | Last push 2018. `seikosantana/lf2-ai-scriptengine` is a modified copy. |
| LF2-Dashboard (xmfcx) | https://github.com/xmfcx/LF2-Dashboard | memory-reading stats viewer | none | v2.0a | matched: `0x0044d020` (game state; our `GX_SCREEN`), `0x00451288` (selected players; our `JOINED_MASK`), `0x00458c94` (player objects) | names only | Last push 2022. |
| IDL (Instant Data Loader) | https://github.com/ahmetsait/IDL | DLL that reloads data in a running game; D struct definitions of the game object | MIT | v2.0a | partial: `gameAddr` `0x00458b00` matches; struct offsets not checked | evidence only (struct layout); vendoring is possible under MIT but nothing here needs it | Active 2025. |
| LF2.IDE | https://github.com/ahmetsait/LF2.IDE | data-file editor and `.dat` encryption | MIT | data format, not the exe | not applicable (no exe addresses) | evidence only (data format) | `tools/re/decrypt_dat.py` is ours. |
| F.LF (Project F) | https://github.com/Project-F/F.LF | clean-room JavaScript reimplementation | GPL-3.0 | behaviour of v2.0a, no binary | not applicable | evidence only | Not derived from the exe; behaviour may differ. |
| html5-lf2 | https://github.com/s911415/html5-lf2 | web reimplementation | GPL-3.0 | behaviour only | not applicable | nothing | |
| lf2gym | https://github.com/elvisyjlin/lf2gym | RL environment over a web LF2 | MIT | web reimplementation | not applicable | nothing | |

Not found (searched 2026-10-13): a matching or full decompilation of `lf2.exe` on decomp.dev, GitHub or GitLab (the GitLab "Little Fighter" topic holds only a data editor); a public Ghidra or IDA database or symbol map; a native PC recompilation. Codeberg search returned its anti-scraper page and was not checked.
