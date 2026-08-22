---
id: 88
title: Device icons cover the pre-fight overlay and the keyboard/gamepad silhouettes need refinement
status: resolved
symptom: A keyboard HUD icon is drawn over the Stage row of the pre-fight overlay; keyboard and controller icons are visually crude
tags: reported,ui,rendering,input,overlay,device-icons
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

Character-select device labels were added from `present_primary`, after every guest blit had
already been recorded. The pre-fight panel does not change the character-select screen word,
so the old state predicate kept appending the icon as the final layer and necessarily put it
above the panel. The previous issue #74 explanation that the overlay raised `SCREEN_MATCH` was
false.

The shared keyboard asset also used a nearly square 62x50 body with four dense rows, which read
as a calculator/emoji at the 18-pixel shipping size. The controller had a crowded centre and a
weak body silhouette.

## What was tried / dead ends

Hiding the icon while an overlay flag is up would remove the symptom, but it would leave the
label as a present-time overlay with no correct owner. The required relation is painter order:
portrait, then device label, then pre-fight panel.

## Resolution

### Resolution (2026-08-22)
Moved character-select device labels into guest painter order immediately before the pre-fight panel; revised the shared 18px keyboard/gamepad silhouettes and verified uncovered/covered 1920x1080 captures.
