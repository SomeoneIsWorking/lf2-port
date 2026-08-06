/* Scripted input: one timing model and one exit report for all three devices.
 *
 * The pad, the keyboard and the mouse each have their own script syntax, and each keeps its
 * own parser here -- what they share is not the syntax but the two things that were only
 * ever right for the pad: WHEN an item fires, and whether anyone finds out that it did not.
 *
 *   LF2_VIRTUAL_PAD    "<button>:<frame>"  or  "<button>@<screen>[+n]"      items split by ,
 *   LF2_VIRTUAL_PAD2   the same, second pad                                 items split by ,
 *   LF2_KEY_SCRIPT     "<vk>:<frame>"      or  "<vk>@<screen>[+n]"          items split by ,
 *   LF2_CLICK_SCRIPT   "<x>,<y>:<frame>"   or  "<x>,<y>@<screen>[+n]"       items split by ;
 *
 * Why the screen form matters more than it looks: a frame number is exact and reproducible
 * WITHIN a run, but the frame a screen ARRIVES on is not -- it moves with the data load and
 * with how busy the machine is. Issue #18 went red three times for that and never once for a
 * real regression. `@<screen>` fires n frames after the game first DRAWS that screen, so a
 * press aimed at the match lands in the match however long the load took.
 */
#ifndef LF2_SCRIPT_H
#define LF2_SCRIPT_H

enum { SCRIPT_PAD0, SCRIPT_PAD1, SCRIPT_KEYS, SCRIPT_CLICKS, SCRIPT_STREAMS };

/* Called once per presented frame, before anything asks script_when(). Notices which of the
 * game's screens are being drawn, which is what the @<screen> form resolves against. */
void script_observe_screens(long frame);

/* Resolve one item's timing spec -- "1200", "@match", "@charselect+58".
 * Returns the frame it fires on, or -1 with *unresolved set when its screen has not appeared
 * YET. Not-yet is not never: a caller must not report on it, only skip it this frame. */
long script_when(const char *spec, int *unresolved);

/* Per-item accounting, so the exit report can say which items did not happen rather than
 * that something did not. Call script_seen for every item parsed, and script_fired at the
 * moment the input actually goes down. */
void script_seen(int stream, int idx);
void script_fired(int stream, int idx);
void script_bad_item(int stream, int idx);   /* unparseable: a name this build does not know */

/* At exit: the screens reached, then one line per configured script with its denominator,
 * then a line naming each item that never fired. */
void script_report(void);

#endif
