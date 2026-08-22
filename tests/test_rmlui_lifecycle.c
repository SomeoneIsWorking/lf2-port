/* RmlUi can close from an input callback inside its own frame.  Exercise the shipping
 * lifecycle rule without a window or RmlUi (issue #94). */
#include "rmlui_lifecycle.h"

#include <stdio.h>

static int failures;

static void check(int ok, const char *what)
{
    if (ok) return;
    printf("  FAIL  %s\n", what);
    failures++;
}

int main(void)
{
    RmlUiLifecycle lifecycle;
    rmlui_lifecycle_init(&lifecycle);
    check(rmlui_lifecycle_frame_begin(&lifecycle) == 0, "a closed document cannot begin a frame");

    rmlui_lifecycle_open(&lifecycle);
    const unsigned open_frame = rmlui_lifecycle_frame_begin(&lifecycle);
    check(open_frame != 0, "an open document begins a frame");
    check(rmlui_lifecycle_frame_continues(&lifecycle, open_frame), "an unchanged document completes its frame");

    rmlui_lifecycle_close(&lifecycle);
    check(!rmlui_lifecycle_frame_continues(&lifecycle, open_frame),
          "a close callback cancels the UI frame that invoked it");

    rmlui_lifecycle_open(&lifecycle);
    const unsigned replaced_frame = rmlui_lifecycle_frame_begin(&lifecycle);
    rmlui_lifecycle_close(&lifecycle);
    rmlui_lifecycle_open(&lifecycle);
    check(!rmlui_lifecycle_frame_continues(&lifecycle, replaced_frame),
          "closing and reopening cannot resurrect the interrupted frame");
    check(rmlui_lifecycle_frame_continues(&lifecycle, rmlui_lifecycle_frame_begin(&lifecycle)),
          "the replacement document owns a new frame");

    if (failures) {
        printf("RmlUi lifecycle: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("RmlUi lifecycle: ok\n");
    return 0;
}
