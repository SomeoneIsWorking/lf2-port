#include "lf2_log.h"
#include "environment.h"
#include "blt_trace.h"

#include <stdio.h>
#include <stdlib.h>

void blt_trace_log(const BltTrace *trace)
{
    if (!lf2_environment_get(LF2_ENV_BLT_FRAME) || !trace) return;

    static long logged_for = -1;
    static long count;
    if (!trace->selected) {
        if (logged_for >= 0) {
            lf2_log_writef(LF2_LOG_INFO, "blt_trace", "bltframe %ld: %ld blits total\n", logged_for, count);
            logged_for = -1;
        }
        return;
    }
    if (logged_for != trace->frame) {
        if (logged_for >= 0)
            lf2_log_writef(LF2_LOG_INFO, "blt_trace", "bltframe %ld: %ld blits total\n", logged_for, count);
        logged_for = trace->frame;
        count = 0;
        lf2_log_writef(LF2_LOG_INFO, "blt_trace", "bltframe %ld: begin\n", trace->frame);
    }
    count++;

    char fill[48] = "";
    if (trace->has_fill) snprintf(fill, sizeof fill, " COLORFILL=%08x", trace->fill);
    lf2_log_writef(LF2_LOG_INFO, "blt_trace",
                   "blt %ld dst=%08x[%dx%d%s] rect=(%d,%d)-(%d,%d) "
                   "src=%08x[%dx%d] srect=%s(%d,%d)-(%d,%d)"
                   " flags=%08x from=%08x%s\n",
                   count, trace->destination, trace->destination_w, trace->destination_h,
                   trace->destination_primary ? " primary" : "", trace->dl, trace->dt, trace->dr, trace->db,
                   trace->source, trace->source_w, trace->source_h, trace->has_source_rect ? "" : "NULL", trace->sl,
                   trace->st, trace->sr, trace->sb, trace->flags, trace->caller, fill);
}

void blt_trace_backdrop(const BltTrace *trace, const BackdropBlit *blit)
{
    if (!trace || !trace->selected || !blit) return;
    lf2_log_writef(LF2_LOG_INFO, "blt_trace",
                   "backdrop native frame %ld mirror=%d dst=(%d,%d)-(%d,%d) "
                   "src=(%d,%d)-(%d,%d)\n",
                   trace->frame, blit->mirror_x, blit->dl, blit->dt, blit->dr, blit->db, blit->sl, blit->st, blit->sr,
                   blit->sb);
}
