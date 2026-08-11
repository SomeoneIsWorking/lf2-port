---
id: C011
kind: claim
status: holds
created: 2026-08-05
tags: test,gamepad,flaky
depends: runtime/input/gamepad.c#bind_available
---

## Claim

A physical controller attached to the machine silently hijacks scripted pad tests: it binds gamepad slot 0, and the front-end menu is driven from slot 0 only, so the scripted route presses into slot 1 and the run never leaves the front end

## Evidence

Run log scratch/logs/screens.log: 'controller 0 connected: Xbox One S Controller' ahead of 'virtual pad 0: attached as joystick 4', then 'input: 0 gathers', only bgm/main.wma loaded, and no screen reached in 2800 frames. With the physical pad ignored (scripted_run() in runtime/input/gamepad.c) the identical script reaches charselect@1083, overlay@1921 and match@2142. gamepad_drive_ui() reads slot[0] only, which is the mechanism.

## What would falsify it

the front end becoming drivable from any bound pad rather than slot 0 -- then a physical controller taking slot 0 would no longer stall a scripted route
