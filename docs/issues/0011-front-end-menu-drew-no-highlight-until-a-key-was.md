---
id: 11
title: Front-end menu drew no highlight until a key was pressed
status: resolved
symptom: the initial menu's keyboard navigation does not highlight items; the first arrow press appears to start on the wrong entry
tags: menu,input,keyboard,controller,highlight
created: 2026-08-05
updated: 2026-08-05
---

The port drives the front-end highlight by writing the pointer onto the selected item, so
the game's own renderer draws it. `menu_sync_from_pointer` handed control back to the mouse
whenever the pointer was somewhere the port had not put it -- and it did that even when the
pointer was nowhere near the menu at all, which is where it sits at boot (the origin).

So the port dropped its selection every frame and never wrote the pointer, and the front end
drew nothing highlighted. The first arrow press then moved from an invisible item 0 to
item 1, which reads as the highlight starting on the wrong entry.

Fix: if no item is within reach of the pointer, the pointer is not what is driving the menu,
so the port keeps its own index and goes on asserting it. A pointer that IS near an item
still wins immediately, so the mouse is unaffected.

Verified from frame dumps: "game start" is highlighted before any input, and two down
presses walk it to "network game" then "control settings".
