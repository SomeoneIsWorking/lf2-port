---
id: 22
title: Pause menu needs EXIT TO MENU and a co-op drop-out option
status: open
symptom: the pause menu offers only RESUME and QUIT GAME: there is no way back to the front end without killing the process, and a joined player has no way to leave a match deliberately
tags: reported,pause,menu,coop,drop-in,ux
created: 2026-08-05
updated: 2026-08-05
---

REPORTED. runtime/pause.c currently has exactly two items:

    enum { IT_RESUME, IT_QUIT, IT_N };
    static const char *const ITEMS[IT_N] = { "RESUME", "QUIT GAME" };

with IT_QUIT calling hostwin_request_quit() -- it kills the process. Wanted:

  EXIT TO MENU    leave the match, return to the front end, keep the process alive
  DROP OUT        a joined player leaves the match deliberately

WHAT IS ALREADY IN HAND for the drop-out half: coop_leave() in runtime/overrides/coop.c is
the exact inverse of the join (gate byte cleared, device selector zeroed, joined-mask bit
cleared) and it already REFUSES for a slot this port did not fill. It is currently only
reached when a pad is unplugged. coop_owns(slot) says whether a slot is this port's to
release. So the menu item is a caller for machinery that exists -- what is NOT decided is
WHOSE drop-out it is when several devices are in the match: the pause menu is one screen but
drop-out is per player, so the item has to name a player or be driven by the device that
opened the menu. pause.c does not currently track which device paused.

WHAT IS NOT KNOWN for exit-to-menu: how the game itself returns from a match to the front
end. The port must not fake it by resetting its own state -- the game has a path for
finishing a match (the overlay's Exit item is one candidate, 0x0044d06c index 5 per
overrides.h) and that path is what should be driven. Reproducing the transition by hand
would leave the game's own state half-wound and is exactly the kind of thing that looks
right until a second match starts.

Note that pause works by declining to call fn_004246b0__orig, which freezes the world with
nothing to save or restore -- so whatever drives the exit has to happen with the game
UNFROZEN, i.e. unpause first, then drive the transition.
