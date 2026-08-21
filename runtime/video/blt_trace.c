#include "blt_trace.h"

#include <stdio.h>
#include <stdlib.h>

void blt_trace_log(const BltTrace *trace)
{
    if (!getenv("LF2_BLT_FRAME") || !trace) return;

    static long logged_for = -1;
    static long count;
    if (!trace->selected) {
        if (logged_for >= 0) {
            fprintf(stderr, "bltframe %ld: %ld blits total\n", logged_for, count);
            logged_for = -1;
        }
        return;
    }
    if (logged_for != trace->frame) {
        if (logged_for >= 0) fprintf(stderr, "bltframe %ld: %ld blits total\n", logged_for, count);
        logged_for = trace->frame;
        count = 0;
        fprintf(stderr, "bltframe %ld: begin\n", trace->frame);
    }
    count++;

    char fill[48] = "";
    if (trace->has_fill) snprintf(fill, sizeof fill, " COLORFILL=%08x", trace->fill);
    fprintf(stderr,
            "blt %ld dst=(%d,%d)-(%d,%d) src=%08x[%dx%d] srect=%s(%d,%d)-(%d,%d)"
            " flags=%08x from=%08x%s\n",
            count, trace->dl, trace->dt, trace->dr, trace->db, trace->source, trace->source_w, trace->source_h,
            trace->has_source_rect ? "" : "NULL", trace->sl, trace->st, trace->sr, trace->sb, trace->flags,
            trace->caller, fill);
}
