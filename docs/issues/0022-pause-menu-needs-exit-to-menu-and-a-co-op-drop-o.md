---
id: 22
title: Pause menu needs EXIT TO MENU and a co-op drop-out option
status: open
symptom: the pause menu offers only RESUME and QUIT GAME: there is no way back to the front end without killing the process, and a joined player has no way to leave a match deliberately
tags: reported,pause,menu,coop,drop-in,ux
created: 2026-08-05
updated: 2026-08-11
---

REPORTED. runtime/app/pause.c currently has exactly two items:

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

### Note (2026-08-06)
RE, 2026-08-06: the top-level mode NEVER goes back -- and the front end was the wrong target anyway.

WHAT THE MODE WORD IS. fn_004246b0 is called with ECX = 0x00458b00 (lf2_recomp.c, the call
at guest 0x0043ecb2), and the mode it switches on is [ECX] -- so the top-level mode is the
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
  WATCH 00458b00 changed 00000000 -> 00000001 ... ret 00422ad2
  WATCH 00458b00 changed 00000001 -> 00000002 ... ret 0043e975
  (nothing further, to the end of the run)

The two boot transitions are the POSITIVE CONTROL: the watch demonstrably fires, so the
silence afterwards is the game not writing rather than the instrument not looking. Route
report: 21 of 21 presses fired (and see #24 -- the report that said otherwise was broken;
this run was re-done against the fixed one before any of this was believed).

So: THE GAME HAS NO PATH FROM A MATCH BACK TO THE LAUNCHER. That half of this entry cannot
be done by driving the game's own transition, because there is no such transition.

BUT THE LAUNCHER IS NOT WHAT "MENU" MEANS. The screen a player calls the main menu is the
POST-LOAD MODE MENU -- VS mode / Stage mode / the two championships / Battle mode / Demo /
Playback Recording / Quit -- and it lives INSIDE mode 2, with its selection at 0x00451160
(screens.c, MODEMENU_SEL). The launcher (screens 0/6/7, mouse-only) is the little setup
screen before the game, which nobody asks to be returned to mid-session.

So the work this entry has left is NOT "get the mode word back to 0". It is: find the state
inside fn_0041bc90 that selects mode menu vs character select vs match, and whether the game
moves it backwards. That is a transition WITHIN mode 2, which is a much more plausible thing
for the game to own -- the mode menu is reached from the load exactly once today, and the
Exit item on the pre-fight overlay already walks one step back (match -> cleared character
select) without touching the mode word at all.

DEAD END, so it is not retried: hunting for a write that returns the mode word to 0. It does
not exist, and the two prior measurements in this entry (Escape does nothing at the cleared
character-select screen, Escape does nothing at the overlay) are consistent with that
without being the reason for it. Escape delivery is not the problem: win32.c maps it to VK
0x1B and port_owns_key withholds it only while a match or the pause menu is up, so the game
does receive it on those screens and does nothing with it.

### Note (2026-08-06)
RE, 2026-08-06 (second pass): the mode menu is a once-per-process screen too, so NO game-owned
transition back to any menu exists. What is left is a design decision, not more RE.

FINDING THE STATE. A controlled .data diff rather than a read of fn_0041bc90's 28 KB: three
frames parked on the mode menu (LF2_VIRTUAL_PAD="south:900" and nothing after it, so the game
sits there) as the noise control, against two character-select frames that agree with each
other. 12745 dwords -> 12740 stable across the three -> 8 that differ from both
character-select frames. Six of the eight are the stack canary (0x0044eea4/a8) and two copies
of a time string; the two real ones are:

  0x0044d070   -100 at the mode menu, 1 from character select onward
  0x00451200      0 at the mode menu, 1 from character select onward

0x0044d070 IS THE GAME MODE, and this project has been burned by it before -- runtime/video/ddraw.c
(~line 703) records an earlier session taking it for a screen word because it separates the
screens perfectly IN STAGE MODE (-100 / 0 / 1), which is where it was both derived and
checked. It reads 1 in VS mode whether the overlay is up or not. So the right reading of the
diff above is not "the screen word is 0x0044d070" but "the mode menu is the screen where no
game mode has been chosen yet".

MEASURED. LF2_WATCH=44d070 over the full route -- launcher, load, mode menu, character select,
overlay, a running match, LEAVE MATCH (F4 at 2389, overlay Exit dispatched), and the frames
after it on cleared character select:

  WATCH 0044d070 changed ffffff9c -> 00000001 ... ret 0042b894
  (nothing further, to the end of the run)

One transition, at the moment VS mode is picked, inside fn_00429730. That transition is the
positive control that the watch fires. Route report: 21 of 21 presses fired.

Statically it agrees: 0x0044d070 is written in exactly three places, all in fn_00429730, all
writing a computed register; the constant 0xffffff9c appears seven times in the whole lifted
binary and only ONCE as a comparison (the test at guest 0x0042b446), never as an assignment.
-100 is the image's initial value, not something the game restores.

SO, TAKEN WITH THE FIRST PASS: the launcher is left once (mode word 0 -> 1 -> 2, never back)
and the game mode is chosen once (-100 -> 1, never back). LF2 has no way back to either menu.
The overlay's Exit walks match -> cleared character select and that is the whole of the way
back that exists.

THIS ENTRY'S ORIGINAL CONSTRAINT IS NOW UNSATISFIABLE AS WRITTEN. It says "the game has a path
for finishing a match ... and that path is what should be driven". It does not. The remaining
choice is a design one and belongs to the person who reported it:

  (a) LEAVE MATCH is the truthful maximum. Close this half, keep the item named for what it
      verifiably does, and the port never claims a screen transition the game does not own.

  (b) The port drives the game back through its OWN ENTRY SEQUENCE rather than inventing a
      transition: game mode <- -100, world mode <- 1, and let fn_004246b0's mode==1 branch do
      what it does at startup (it loads, then sets mode 2, and the game arrives at the mode
      menu). Note this is not the same as stamping a screen number -- every step is the
      game's own -- and the world object even has its own reset to hand, fn_00419e40, which
      is [this]=0 plus a memset of the 400 gate bytes and is what the static initialiser
      calls. The risks are real and should not be waved through: the data load re-runs (1.2 s
      today), and what else the game leaves wound from a finished match is NOT established --
      music, the object registry beyond the gate bytes, and the character-select roster are
      each unproven.

RECOMMENDATION: (a) unless the reporter wants (b) enough to pay for establishing what a
second load does to state that the first one set up. (b) is a day's work with a real chance
of a half-wound second match, which is exactly the failure this entry warned about; it is not
a thing to try casually and call done because the menu appeared.

### Note (2026-08-11)
DECIDED 2026-08-11 by the reporter, and the framing of the note above was wrong: "it's a design
call" -- "no it's not". Option (b). Port the game's own entry sequence; do not close this at
LEAVE MATCH.

So the work is: game mode <- -100, world mode <- 1, and let fn_004246b0's mode==1 branch do
what it does at startup -- it loads, sets mode 2, and the game arrives at the mode menu. Every
step is the game's own; nothing stamps a screen number. The world object has its own reset to
hand, fn_00419e40 ([this]=0 plus a memset of the 400 gate bytes), which is what the static
initialiser calls.

WHAT THE PREVIOUS NOTE LISTED AS RISKS STILL HAS TO BE ESTABLISHED -- they are work items now,
not reasons to hesitate: what a finished match leaves wound that a second load does not clear.
Named suspects, none of them measured: the music, the object registry beyond the 400 gate
bytes, and the character-select roster. The way to establish it is a second match after an exit,
compared against a first match in a fresh process.
