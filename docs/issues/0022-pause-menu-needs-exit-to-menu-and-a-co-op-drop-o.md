---
id: 22
title: Pause menu needs EXIT TO MENU and a co-op drop-out option
status: resolved
symptom: the pause menu offers only RESUME and QUIT GAME: there is no way back to the front end without killing the process, and a joined player has no way to leave a match deliberately
tags: reported,pause,menu,coop,drop-in,ux
created: 2026-08-05
updated: 2026-08-17
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

### Note (2026-08-11)
OPTION (b) TRIED AND IT DOES NOT ARRIVE AT THE MODE MENU. The code was written, run, and
REVERTED -- what survives is this finding and the two addresses in runtime/overrides/world.h.

WHAT WAS DONE, exactly as the note above specified it: after the overlay's Exit is dispatched
and the match is over (panel_hud_up() false), write

    [0x0044d070] <- -100     the game mode, back to the image's initial value
    [0x00458b00] <- 1        the top-level mode, the load branch

and let fn_004246b0's own mode==1 branch do the rest. Nothing stamped a screen number.

WHAT HAPPENED. The writes land -- the run reports "top mode 2 -> 1, game mode 1 -> -100" at
frame 2290 -- and 310 frames later the game is STILL ON THE CLEARED CHARACTER-SELECT SCREEN
(scratch/exit22g/frame_002600.ppm: the "Character Selection" panel with all eight slots reading
"Press Attack to join!"). It is not the mode menu and it is not the launcher.

WHAT THAT ELIMINATES, which is the value here. The mode word alone does not select the screen.
The likely mechanic is that fn_004246b0's mode==1 branch runs and immediately sets mode 2 again
-- it is the ONLY write of 2 in the binary -- so the top-level mode is back where it was within
a frame, while whatever selects mode-menu-vs-character-select inside mode 2 was never touched.
That is consistent with the debug output: LF2_MENU_DEBUG prints on CHANGE and printed nothing
after the write, which is what a 2 -> 1 -> 2 round trip inside one frame looks like.

SO THE SECOND RE NOTE ABOVE WAS INCOMPLETE where it mattered. It established that the game mode
is -100 at the mode menu and 1 afterwards, and inferred that restoring it plus the load branch
would return there. The inference is wrong: -100 is a NECESSARY condition for the mode menu,
not a sufficient one.

WHAT IS ACTUALLY LEFT, and it is what that note itself named before drawing the wrong
conclusion: find the state inside fn_0041bc90 (28 KB, the game's main dispatcher) that selects
mode menu vs character select vs match within mode 2. The controlled .data diff that found
0x0044d070 and 0x00451200 is the method that works here -- but it must be run against the right
PAIR this time: the cleared character-select screen AFTER an exit, against the mode menu, with
a stable control. 0x00451200 was the other word that differed and it has never been followed up;
it read 0 at the mode menu and 1 from character select onward, and unlike 0x0044d070 it has no
known second meaning.

DO NOT retry the mode-word write. It is measured, it lands, and it does not do this.

### Note (2026-08-11)
THE .data DIFF IS DONE -- the one this entry has twice named as the next step. Candidates
below, with denominators. Nothing has been WRITTEN yet: a diff produces suspects, and the test
is to write one and watch, which is where the next session starts.

METHOD, and it has a control on both sides rather than one. Three frames parked on the MODE
MENU (LF2_VIRTUAL_PAD="south:900" and nothing after, so the game sits there) against three
frames on the CLEARED CHARACTER-SELECT screen reached by the port's own exit -- match, F4,
overlay Exit. Anything not stable across all three frames of BOTH runs is noise and is dropped
before the comparison.

    12745 dwords compared
    12740 stable across the three mode-menu frames
    12740 stable across the three post-exit frames
    12740 stable in both
       71 differ between the two screens

Of those 71, most are self-evidently not a screen word: ASCII (a WMA filename, "Music: Random",
"VS mode (Difficult)", a date), the stack canary pair at 0x0044eea4/a8, and live match values
(the camera at 0x00450bc4, its easing at 0x00450bc8). What is left is small integers that are
ZERO at the mode menu:

    0x00450b80   0 -> 1
    0x00450b90   0 -> 1
    0x004511fc   0 -> 1
    0x00451200   0 -> 1        <- the lead from the earlier note, now confirmed to differ
    0x0045115c   0 -> 2
    0x00451220 through 0x00451244, ten consecutive dwords, 0 -> 1
    0x004554d0   0 -> 4
    0x00452180   0 -> 13       0x00452194  0 -> 185     0x00452274  0 -> 32

TWO READINGS TO TELL APART BEFORE WRITING ANYTHING, because they call for opposite actions:
  - The run of ten consecutive dwords at 0x00451220..44 is the shape of a PER-SLOT array, not a
    screen word -- eight player slots plus two. If that is the character-select roster then it
    is a consequence of the screen rather than its cause, and zeroing it will clear the panels
    while leaving the screen up.
  - 0x004511fc and 0x00451200 sit immediately below it and are a PAIR. MODEMENU_SEL is already
    known to live at 0x00451160 (screens.c), so this whole region is the post-load screen's
    own state and the pair is the likeliest cause rather than consequence.

AND ONE THING THE DIFF ALREADY SETTLES: 0x0044d06c reads 2 at the mode menu and 5 after the
exit -- that is OVERLAY_SEL holding the Exit item the port itself just selected. It is the
port's own fingerprint in this data, not a game state, and it must not be chased.

HOW TO TEST A CANDIDATE: write 0 to it on the frame after the exit completes and see whether
the mode menu appears. That is a one-line change in exit_reenter (reverted, see the commit) and
about fifteen seconds a run. Do NOT write several at once -- the point of the list is that they
can be told apart.

### Note (2026-08-11)
STOP -- THE DIFF ABOVE IS MISLABELLED AND THE SIX CANDIDATES WERE TESTED AGAINST A BROKEN
DISCRIMINATOR. Neither result should be believed. Correcting both in place, because the note
above reads like progress and would send the next session down a dead end.

WHAT WENT WRONG. The run I called "parked on the MODE MENU" -- LF2_VIRTUAL_PAD="south:900",
frames 1400/1500/1600 -- is NOT on the mode menu. Its frame 1500 dumped to a picture, and the
picture is the CHARACTER SELECTION panel with all eight slots on "Press Attack to join!". It is
pixel-for-pixel the same screen as the post-exit side: the positive control I ran afterwards
says 0.0% of pixels differ between them.

So the diff was character-select-with-game-mode--100 against character-select-after-an-exit. It
is a real pair and its 71 differences are real, but it is not the pair the note claims and it
cannot isolate a screen word, because BOTH SIDES ARE THE SAME SCREEN.

THE SIX CANDIDATE TESTS INHERIT THAT. 0x00451200, 0x004511fc, 0x0045115c, 0x00450b80,
0x00450b90 and 0x004554d0 were each zeroed after the exit (all six held a non-zero value first,
so all six were real writes -- the probe reports that) and all six left the screen 0.2%
different from the cleared character-select frame. I read that as six negatives. It is not: a
comparison whose positive control reads 0.0% cannot report a screen change in either direction.
Those six are NOT eliminated. They are untested.

WHAT THIS DOES ESTABLISH, and it is worth having: at frames 1400-1600 of a run that pressed
attack once at frame 900, the game mode word reads -100 -- no mode chosen -- while the screen
being DRAWN is the character-select panel. docs/running.md already says the post-load panel and
the mode menu share a blit destination; this shows they can share a PICTURE too, at least in
the state that run was in. Which means "which screen is up" cannot be read off a frame dump by
comparing it with another dump, and every method in this entry that depends on telling those
two apart needs a signal that is not the picture.

WHAT THE NEXT SESSION MUST DO FIRST, before any more diffing or probing: get a frame that is
DEMONSTRABLY the mode menu -- the VS / Stage / Championship list -- and know what drove the
game there. Until that exists there is no positive control, and without one every result here
is the same shape as the two above: a measurement that cannot fail.

The probe itself is kept and is sound: LF2_EXIT_PROBE=<hex>[,<hex>...] zeroes those guest
addresses a few frames after the exit completes and reports what each held first, saying
explicitly when a word was ALREADY zero so that writing nothing cannot read as a negative. It
costs a run per candidate rather than a rebuild. It is the discriminator downstream of it that
was missing.

### Note (2026-08-12)
THE POSITIVE CONTROL EXISTS NOW. This entry said: 'WHAT THE NEXT SESSION MUST DO FIRST, before
any more diffing or probing: get a frame that is DEMONSTRABLY the mode menu -- the VS / Stage /
Championship list -- and know what drove the game there. Until that exists there is no positive
control, and without one every result here is the same shape: a measurement that cannot fail.'

Done, and it took two things that did not exist when this was written:

  panel_modemenu_up()   issue #51 -- the mode menu identified by the full-screen colour only it
                        paints (0x122565, one site in the whole binary), rather than by the game
                        MODE word that reads 1/4/5 in a match
  @modemenu             a route anchor over that signal, so a dump can be aimed at the screen
                        instead of at a frame number

And the screen is reachable and held: LF2_MODE holds the mode menu open (it reports doing so),
so a dump at @modemenu+20 lands squarely on it rather than in the frame or two it is up for
otherwise (issue #59 -- it IS drawn in every run, at frame 5).

    screens reached -- frontend@1 modemenu@6 charselect@7 overlay@427
    mode-menu frame vs character-select frame: 65507/436700 px differ (15.0%)

FIFTEEN PER CENT, against the 0.0% that made every earlier comparison in this entry vacuous.
The two screens are now distinguishable by picture, which is what the whole method needed.

WHAT THAT UNBLOCKS, in order: the six candidate words -- 0x00451200, 0x004511fc, 0x0045115c,
0x00450b80, 0x00450b90, 0x004554d0 -- are recorded above as UNTESTED, not eliminated, because
they were judged against a comparison that could not fail. Each can now be re-run with
LF2_EXIT_PROBE and judged against a real 15% baseline: a word whose zeroing puts the game on the
mode menu should move the picture toward the mode-menu frame, and one that does nothing should
leave it on character select. That is a discriminator with both classes available for the first
time in this entry's life.

### Note (2026-08-12)
THE SIX CANDIDATES ARE ELIMINATED -- legitimately this time, with the discriminator run against
BOTH classes before any of them was believed.

    LF2_EXIT_PROBE_SELFTEST=1   exit probe: THE MODE MENU CAME UP within 240 frame(s)
                                (screens reached -- frontend@1 modemenu@6 charselect@7)

    00449060  (control)         the mode menu did NOT appear in the 240 frame(s) after the write
    00451200                    did NOT appear
    004511fc                    did NOT appear
    0045115c                    did NOT appear
    00450b80                    did NOT appear
    00450b90                    did NOT appear
    004554d0                    did NOT appear

Every one of the seven writes HAPPENED -- the probe reports what each word held first and all
seven were non-zero (93, 1, 1, 3, 1, 1, 4), so none of these is the "wrote a word already zero"
non-result the entry warns about.

WHAT MAKES THIS DIFFERENT FROM THE FIRST TIME THEY WERE "ELIMINATED": the earlier round judged
them by diffing frames against a control that read 0.0%, and the entry correctly retracted it --
"those six are NOT eliminated, they are untested". This round replaces the diff with
panel_modemenu_up() (issue #51) and, crucially, PROVES THE VERDICT CAN SAY THE OTHER THING:
LF2_EXIT_PROBE_SELFTEST arms the watch at frame 0, where the mode menu is drawn at frame 5 in
every run (issue #59), so the positive branch must fire -- and does.

AND THE INSTRUMENT WAS DEAD ON ITS FIRST RUN, silently. exit_probe zeroes exit_state when it
writes, and exit_to_menu_tick begins `if (!exit_state) return;` -- so the watch, ticked below
that line, never ran. Seven candidate runs printed their write and then said NOTHING. That reads
as "no verdict yet" rather than "the instrument is dead", which is why the tick now runs BEFORE
the early-out and why the self-test exists at all.

WHERE THIS LEAVES THE ENTRY: the six words are gone, and the .data diff that produced them came
from a mislabelled pair, so the LIST ITSELF is suspect rather than incomplete. The next step is
not more candidates from that diff -- it is a fresh diff, now that a frame which is demonstrably
the mode menu can be produced on demand (@modemenu, LF2_MODE holds it) and compared against the
screen the exit actually reaches.

### Note (2026-08-12)
THE EXIT ALREADY REACHES THE MODE MENU. It has all along. It passes through it in ONE FRAME and
lands on character selection, which is why every attempt to find "the word that gets us there"
was looking for something that was never missing.

LF2_FRAMING_DEBUG logs each screen as it paints its own backdrop colour. Around the exit:

    frame 1404   fill 122565   THE MODE MENU        <- the exit's own doing
    frame 1405   fill 000000   character selection  <- one frame later
    frame 1433   fill 122565   the mode menu again  <- the probe's write
    frame 1602   fill 000000   character selection  <- after a press at 1600

So the entry's standing conclusion -- "OPTION (b) TRIED AND IT DOES NOT ARRIVE AT THE MODE MENU"
-- is wrong. It arrives. It does not STAY.

AND THE RECOVERED MENU IS FUNCTIONAL, not just drawn: the press at frame 1600 moved it on to
character selection at 1602. Whatever state the exit leaves behind, the mode menu built on it
takes input and dispatches normally.

WHAT THE PROBE FOUND ANYWAY, and it is worth keeping: 0044d020 = 10 brings the mode menu up on
demand. That is the screen-state word this entry named long ago as "the honest identifier"
(FUN_00431d10 reached from FUN_00429730 when DAT_0044d020 == 10) and never tested -- every round
of diffing chased other words. Thirteen single-word candidates were tested against it and every
one came back negative, with the positive control firing, so the discriminator was working the
whole time; the address was simply not among the candidates the diffs produced.

SO THE REAL DEFECT IS A ONE-FRAME OVERSHOOT, and the likely cause is in the port's own exit
sequence rather than in the game: exit_to_menu_begin sets the overlay's selection to Exit and
ISSUES A DEVICE ATTACK to make the game dispatch it (the run log says so). If that synthesized
press is still live when the mode menu appears a frame later, the menu consumes it and confirms
its first item -- which lands exactly where this run lands.

WHAT TO DO NEXT, and it is a measurement first: log the injected press's lifetime against the
frame the mode menu appears. If they overlap, the fix is to stop issuing input once the overlay
has dispatched, not to write any word. That would make this a port bug with a small fix rather
than the RE hunt it has been.

### Note (2026-08-12)
THE SCREEN WORD'S SEQUENCE, MEASURED, and it settles somewhere the entry never considered.

0x0044d020 read straight out of .data dumps across the transition (no code change, one run):

    frame 1400    0044d020 = 1
    frame 1403    = 1
    frame 1404    = 10      <- the MODE MENU, for one frame
    frame 1405    = 3
    frame 1406    = 1
    frame 1450    = 1       <- and it stays there

So the exit does not overshoot by accident and it is not the port's synthesized press. The
game walks 1 -> 10 -> 3 -> 1 and SETTLES on 1, which is character selection. The mode menu is a
state it passes through on the way somewhere else.

THE SYNTHESIZED-PRESS HYPOTHESIS IS REFUTED, not merely unproven. The previous note guessed the
confirm's second gather was landing on the menu; a cancel was written that fires the moment
panel_modemenu_up() goes true, and it NEVER FIRED -- the two gathers are already spent by frame
1404. The change was reverted rather than left in as a plausible-looking no-op.

WHAT THIS MEANS FOR THE FEATURE. The reporter asked for "leave the match, return to the front
end, keep the process alive". The GAME's own answer to leaving a match is character selection --
that is where its state machine settles, and it is what a player would get pressing Exit in the
original. So the port's exit already does what the game does, and the entry's target is a step
FURTHER BACK than the game goes on its own.

That is a design question again, but a much better posed one than before: not "why does our exit
miss the mode menu" (it does not miss it -- the game leaves it), but "should EXIT TO MENU stop
where the game stops, or carry on to the front end". Both are implementable; only the second
needs anything beyond what already works.

AND 0044d020 = 10 HOLDS when written directly: the probe run left the mode menu up and it took
input normally (a press moved it to character selection). So if the answer is "stop at the mode
menu", the mechanism exists and is one word -- the game's own screen state, the same kind of
write LF2_MODE already makes into the mode menu's selection.

### Note (2026-08-12)
### RESOLVED (2026-08-12) -- and the note above it is WRONG

The previous note read a sampled sequence of 0x0044d020 (`1 -> 10 -> 3 -> 1`) and concluded
"the game walks there on its own... THE SYNTHESIZED-PRESS HYPOTHESIS IS REFUTED". Both halves
are wrong, and they are wrong in the way issue #61 names: a sampled state word cannot
distinguish "the code goes there" from "an input drove it there". The decompilation can, and
does, in three lines.

**What the code says.** `fn_0041bc90` runs the match while 0x0044d020 is 0 and otherwise hands
it BY ADDRESS to `fn_00429730`, which dispatches on it: 1 = character selection, 2 and 3 enter
it, **10 = the FRONT-END MENU** (`fn_00431d10`, the eight-item list), 0x14..0x32 / 0x78..0x96 /
200..299 / 300 = the other panels. `fn_00429730` also passes `&0x00451160` to `fn_00431d10`,
which uses that one word as BOTH the front-end cursor (0..7) and the game mode -- picking the
item is picking the mode.

`fn_00431d10` writes the screen word in exactly one place: its confirm branch. Cursor 0, 1 or 4
sets it to **3**, which `fn_00429730` turns into **1**. So `10 -> 3 -> 1` is not a walk. It is
one confirm press on the front-end menu with the cursor on item 0, and nothing else in the
binary produces that pair of writes.

**What raises that confirm.** `fn_00431b70`, the menu's gather: per player it reads obj+0xcd..
0xd3 -- the same seven bytes `runtime/overrides/input.c` writes as `BTN_CUR` -- and raises
0x004513b4 (confirm) only on an EDGE, when the latch at 0x00451320 was clear. `fn_00431c70`,
which the game runs on every way out of a match, zeroes both that latch and those seven bytes.

**So the cause.** `input_synth_confirm(exit_dev, 2)` held the attack for two gathers
unconditionally, and outside the game proper `fn_00419a60`'s override routes every device to
slot 0 (`target = in_game ? dev_player[d] : 0`). Gather one dispatched the overlay's Exit;
gather two landed after the transition, on the front-end menu, with the latch freshly cleared
by `fn_00431c70` -- a fresh press by construction. The exit reached the front end correctly and
was then pushed off it by its own press.

Why the earlier cancel "never fired": it was armed on `panel_modemenu_up()`, a DRAWING signal.
The write to 3 happens in the same frame the menu first runs, so a cancel keyed to the menu
being drawn is one frame late by construction. That is a diagnostic that could not have printed
the positive -- the exact failure mode the global rules describe.

**The fix.** A synthetic press now belongs to the screen it was issued on: `input_synth_confirm`
records 0x0044d020 and `synth_active()` drops the remaining gathers the moment it moves, saying
so out loud. That is the game's own rule (`fn_00431c70`), not a shorter frame count.

**Verified**, `tools/e2e.sh exit_to_menu` (new): LANDED on screen 10, the front-end menu, with
"1 gather(s) left when the game moved from screen 1 to screen 10 -- dropped". Run against BOTH
classes: with the scope check compiled out the same route lands on screen 1, character
selection, and the route FAILS. `ctest` 10/10.

`LF2_EXIT_PROBE` and its `exit_probe*` machinery are deleted (issue #61) -- the .data-word hunt
it existed for was answered by one decompilation. `SCREEN_WORD`, `MENU_CURSOR` and the screen
numbering are recorded in `runtime/overrides/world.h`.

### Resolution (2026-08-12)
The exit reached the front-end menu (screen word 0x0044d020 = 10) and was pushed off it by its own synthetic confirm: input_synth_confirm held the attack for two gathers, and fn_00431c70 clears the held-button latch across a screen change, so the leftover gather read as a fresh press to fn_00431b70 and fn_00431d10 confirmed cursor 0. The press is now scoped to the screen it was issued on. Verified by tools/e2e.sh exit_to_menu, which lands on 10 and fails on 1 with the scope compiled out.

### Note (2026-08-17)
SUPERSEDED (2026-08-17): the synthetic-press mechanism this entry describes was itself replaced. LEAVE MATCH now calls the game's own exit code directly (screens.c's guest_end_match / guest_overlay_exit, read off fn_0041bc90 and fn_00429730): fn_00416cd0(0x44d020,0x451160) + fn_00431c70(this) to end the match, then fn_00401a30(0x455610,0) + screen=10 + 0x457580=0 + fn_00431c70(this) for the overlay Exit. input_synth_confirm and any_playing_device are DELETED -- no keystroke or button is injected, so the 'second gather confirms the front-end menu' bug class cannot recur because there is no press. tools/e2e.sh exit_to_menu (updated for the native flow) still lands on screen 10.
