---
id: 134
title: A focused JOIN is stranded when a game above it leaves the open games
status: resolved
symptom: With two games listed and JOIN focused on the second, the first leaving hides the focused row; the player's press joins nothing.
state_items: S022
tags: netplay,lobby,ui
created: 2026-11-12
updated: 2026-11-17
---

Reproduce: `online_meeting_test.py --leaving-game other`. `held_lobby_proxy.py --decoy` lists
a decoy ahead of the host's game until the friend has dumped two frames, then drops it. The
friend focuses the host's game on the second row; after the decoy leaves, it presses JOIN.
On 4d6d184 the friend never asks for a seat: focus stays on the second row, now hidden.

Cause: `runtime/ui/online_ui.cpp:OnlinePage::update` replaced the list without moving RmlUi
focus, and `rmlui_render` ran it before input, so a press could also act on a list the player
had not yet seen.

Fix: `rmlui_render` runs `OnlinePage::update` after input, so a press acts on the rows as
drawn. `update` then moves focus by game id to the game's new row, or to the row that takes a
departed game's place, or to REFRESH when the list is empty. `script.c` gains `@lobbyleft`,
the first frame the page lists fewer games than it had. Both `--leaving-game other` (focus
follows the host's game up a row) and `--leaving-game focused` (the focused decoy leaves and
the host's game takes its row) ask once, join and play with equal hashes.
