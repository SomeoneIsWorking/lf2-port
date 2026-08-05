# LF2 port — working agreement

## The active queue is `docs/issues/`, and it is the answer to "work on the active issues"

```sh
python3 ~/.claude/skills/issue-catalog/catalog.py list --tag reported --status open   # THE QUEUE
python3 ~/.claude/skills/issue-catalog/catalog.py list --status open                  # everything open
python3 ~/.claude/skills/issue-catalog/catalog.py show <id>
```

- **"work on the active issues" / "the issues" / "what's open" means exactly the first
  command.** Run it before asking what to do next, and work the entries in it. Do not invent
  a separate list, a TODO file, or an in-conversation plan that outlives the conversation —
  there is one queue and it is in the repo, so it survives compaction and reaches subagents.

- **Anything the user REPORTS gets filed the moment it is reported**, before the fix is
  attempted, tagged `reported`. A bug seen in play, a design objection ("that shouldn't be
  behind an env var"), a "this should work differently" — all of it. Filing first is what
  stops a report being lost when the session turns to something else, and it is what makes
  the queue an honest picture of what is owed.

- **File the CAUSE and the constraint, not just the symptom.** An entry that says only what
  went wrong makes the next session re-derive the investigation. Say what was measured, what
  is still unknown, and — where it applies — which obvious fix is WRONG and why, so nobody
  ships it. Issue #19 is the shape to copy.

- **Close entries by editing them, not by leaving them.** `catalog.py resolve <id> "<what
  actually fixed it>"`, in the same commit as the fix. A resolved entry with the real cause
  in it is worth more than a closed one that says "done".

## Verification, and what a red test does and does not mean

- **A failing route-scripted test is not evidence of a regression until it has been
  reproduced against a stashed or committed tree.** These tests drive the game through its
  menus, and the routes were frame-numbered, so a busy machine moved them (issue #18). Two
  failures were nearly attributed to a change that had nothing to do with them.

- **Prefer `button@match+30` to `button:2300` in pad scripts.** The `@<screen>` form fires
  off the game's own drawing (`charselect`, `overlay`, `match`), so a press aimed at the
  match lands in the match however long the data load took.

- **Never claim a green that was not run.** If the thing that would verify a change is
  itself broken, say so and leave the work uncommitted or the commit message honest.

## Env vars are for DIAGNOSTICS, never for features

A feature nobody can find is not a feature. Drop-in coop was behind `LF2_COOP=1` and had to
be turned on to exist; widescreen still takes its width from `LF2_WIDESCREEN` instead of the
window (issue #20). The `LF2_*` namespace is for probes, dumps, traces and test scaffolding —
if a player would want it, it belongs in the game's own behaviour or its menus.
