---
id: 22
title: Pause menu needs EXIT TO MENU and a co-op drop-out option
status: closed
symptom: the pause menu offers only RESUME and QUIT GAME: there is no way back to the post-load mode menu without killing the process, and a joined player has no way to leave a match deliberately
tags: reported,pause,menu,coop,drop-in,ux
created: 2026-08-05
updated: 2026-09-09
---

### Note (2026-08-05)
PROGRESS, 2026-08-05: DROP OUT is done; LEAVE MATCH is done as far as it is established;
EXIT TO THE MODE MENU is NOT, and that is what this entry stays open for.
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
  original guest routine 0x004246b0, so a transition driven while frozen would be delivered to a game that
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

### Note (2026-08-06)
RE, 2026-08-06: the top-level mode NEVER goes back -- and the front end was the wrong target anyway.
WHAT THE MODE WORD IS. The retail call at guest 0x0043ecb2 invokes fn_004246b0 with
ECX = 0x00458b00, and the mode it switches on is [ECX] -- so the top-level mode is the
FIRST DWORD OF THE WORLD OBJECT, the same object coop.c calls `this`. Values: 0 = the
launcher, 1 = loading, 2 = the game proper.
WHO WRITES IT, exhaustively over the lifted binary:
  0  fn_00419e40, which is the world object's constructor -- [this]=0 plus a memset of 400
     bytes at this+4, i.e. the gate bytes. Reached only from fn_00446300, which has no
     caller in the lifted set: a static initialiser that runs once at startup.
  1  at guest ret 0x00422ad2 (the launcher's start-game path).
  2  fn_004246b0's own mode==1 branch, ST32(R(ESI), 0x2u) -- the ONLY write of 2, and the
     only write to the word anywhere in fn_004246b0.
There is no site that writes it back.
MEASURED, not just read. LF2_WATCH=458b00 over a full route -- launcher, load, character
select, overlay, a running match, then LEAVE MATCH (F4 at frame 2389, overlay Exit
dispatched) and ~1000 further frames on the cleared character-select screen:
  watching 00458b00 = 00000000

(full record: git history)
