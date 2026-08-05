---
id: 10
title: Click on 'game start' leaked across the data load and started VS mode by itself
status: resolved
symptom: clicking game start on the launcher goes straight into a VS match without the mode menu being clicked
tags: menu,input,mouse,regression,click-edge
created: 2026-08-05
updated: 2026-08-05
---

Self-inflicted, by the fix that made `LF2_CLICK_SCRIPT` arm the port's click edge.

`hostwin_mouse_clicked()` is a one-shot edge consumed by the reader. The front-end launcher
does its own hit-testing through the game's own click flag and never calls it, so a click on
"game start" armed the edge and nothing took it. It then survived the whole data load and
was collected by the first ported menu that looked -- `modemenu_mouse`, with the pointer
still resting where "game start" had been, at (403,228). That is inside the mode menu's
"VS mode" band. One click on the launcher started a VS match.

Fix: the edge expires. A click is a per-frame event; if no ported menu claimed it in the
frame it happened it was not for one, and holding it is how a stale input reaches a screen
that did not exist when the button went down. One frame of slack, because the menu override
and the present do not run in a fixed order within a frame.

Worth noting how it got in: the click edge had NEVER fired in a scripted run before, so
adding it exposed a latent hole in the edge's lifetime that had simply never been reachable.
The mouse test now covers the launcher click, and the route stops on the mode menu instead
of falling through it.
