/* Hand-written native replacements for recompiled functions.
 *
 * Each function here provides the symbol fn_<addr> that the lifter would otherwise have
 * generated, for an address listed in re/overrides.txt. They run in the guest ABI: the
 * caller's arguments are on the guest stack, and a stdcall callee pops them.
 */
#include "guest_ops.h"
#include "hostwin.h"

#include <stdio.h>
#include <string.h>

/* Nothing overridden yet.
 *
 * A note from the first attempt, so it is not repeated: fn_004242e0 looked like the ad
 * strip -- it references http://www.littlefighter.com/advertise and ShellExecute's "open"
 * verb, and its hit-test constants (x 145..775, y 535) match the "To advertise on LF2"
 * link exactly. It is not. It is a general drawing helper, called seven times from the
 * menu draw path, and stubbing it garbled the character artwork along with the ad.
 *
 * Referencing an ad URL is not the same as being the ad function. The thing to port is the
 * caller that decides to draw an ad, not the helper it draws through.
 */
