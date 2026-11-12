---
id: 132
title: The friend sometimes never asks a listed game for a seat
status: resolved
symptom: One run of four of online_meeting_test.py --zerotier ended with the friend never asking (no [lobby] line) and never joining; the friend's pad pressed JOIN at modemenu+500.
state_items: S022
tags: netplay,lobby,test
created: 2026-11-12
updated: 2026-11-12
---

Cause: the test, not the product. The friend's route in `tools/routes/online_meeting_test.py`
pressed its six `down`s at modemenu+300 to +450 and JOIN at +500, timed from the menu rather than
from the list. `OnlinePage::update` shows a JOIN row only once `LobbyList` has listed the game, and
RmlUi focus moves only through rows that exist. A listing that reached the friend after the last
`down` therefore left focus on REFRESH, and the press refreshed, which logs nothing.

Shown by `online_meeting_test.py --held-listing`. `tools/routes/held_lobby_proxy.py` answers the
friend's list requests with no games until the friend has written two frame dumps
(modemenu+130 and +460). On the old route the friend never asked, and its +460 frame reads
"No open games yet".

Fix: `script.c` gains the `@lobbies` anchor, the first frame the ONLINE page lists a game
(`script_observe_lobbies`, called from `OnlinePage::update`). The friend's `down`s and JOIN are
keyed to it. With the listing held, the route asks and joins, and its frame at the press shows
JOIN focused on the listed game. Direct and `--zerotier` runs pass 12/12.

While checking what a player sees when the list changes under the cursor, one product defect
turned up. `LobbyList::list_now` rebuilt the list in directory order, so a refresh could put a
different game under a focused JOIN row. It now keeps each still-listed game on its row and adds
new ones after. A game that leaves the list still moves the rows below it up.
