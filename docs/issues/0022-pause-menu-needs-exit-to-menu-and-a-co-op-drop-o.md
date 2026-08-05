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

### Note (2026-08-05)
PROGRESS, 2026-08-05: DROP OUT is done; LEAVE MATCH is done as far as it is established;
EXIT TO THE FRONT-END MENU is NOT, and that is what this entry stays open for.

WHAT SHIPPED

  RESUME / DROP OUT / LEAVE MATCH / QUIT GAME, with the rows built per pause rather than
  from a fixed table.

  DROP OUT is per player, which is what this entry said was undecided. The menu now records
  WHICH DEVICE opened it -- Escape means the keyboard, Start means the pad holding it
  (gamepad_start_index, new) -- and the item only appears when device_player() of that
  device is a slot coop_owns(). It calls coop_leave through coop_drop_out(). Verified: a pad
  joined mid-match at slot 2, locked in a character, then pressed Start, Down, A on its own
  pad, and the run printed 'coop leave: slot 2 dropped out (the player chose to drop out
  from the pause menu) -- gate cleared, devsel -> 0, joined mask 00000007 -> 00000003'.

  LEAVE MATCH drives the GAME's own way out and nothing of the port's own. That way out is
  F4 -- established by pressing it in a scripted run and looking: scratch/exit22/
  frame_002400.png is the character-select screen with the pre-fight overlay open, from a run
  that was in a fight forty frames earlier. The second step places the overlay's own Exit
  (OVERLAY_SEL index 5) and lets the game's attack button dispatch it, the same contract the
  overlay's mouse handling already uses.

  Both items UNPAUSE FIRST, as this entry required: pausing works by declining to call
  fn_004246b0__orig, so a transition driven while frozen would be delivered to a game that
  never runs another frame.

WHAT THE SECOND STEP ACTUALLY REACHES, and why the item is not called EXIT TO MENU: the
game lands on its own character-select screen with the roster CLEARED and every slot back to
'Press Attack to join!' (scratch/exit22e/frame_002500.png). That is a real exit from the
match -- the process lives, another match can be started -- but it is not the front end.

RULED OUT, so nobody retries it:
  - Escape at that cleared character-select screen does NOTHING (scratch/exit22f/
    frame_002560.png, unchanged 160 frames after the press). The port is not eating it:
    port_owns_key only withholds Escape while panel_hud_up() or the pause menu is up.
  - Escape while the pre-fight overlay is up does nothing either (scratch/exit22c).
  - LF2_OVERLAY_FORCE is NOT a way to test overlay items in a route that also has to reach a
    match: it pins the selection for the WHOLE run, so the press meant for 'Fight!' activates
    the forced item instead and the run never starts a match.

STILL UNKNOWN, and it is the whole remainder: what the game does to go from character
selection back to the front-end menu. It is a screen transition the game must own, and the
port must not fake it by resetting its own state.

ALSO ESTABLISHED ON THE WAY, and worth keeping because it explains a whole class of
'the press did nothing': inside the game proper a device's buttons only reach the game
through the player slot it claimed, so a synthetic press from a device with no slot is
silently thrown away. A scripted run pressing the keyboard's own arrows and attack at the
overlay moved the selection not at all (scratch/exit22d/frame_002520.png, still 'Fight!'),
because the keyboard had claimed no slot. exit_to_menu_begin now falls back to
any_playing_device() and says which device it used and why.
