#ifndef BLT_TRACE_H
#define BLT_TRACE_H

#include <stdint.h>

#include "backdrop.h"

typedef struct {
    long frame;
    int selected;
    uint32_t destination;
    int destination_w, destination_h;
    int destination_primary;
    int dl, dt, dr, db;
    uint32_t source;
    int source_w, source_h;
    int has_source_rect;
    int sl, st, sr, sb;
    uint32_t flags;
    uint32_t caller;
    int has_fill;
    uint32_t fill;
} BltTrace;

/* LF2_BLT_FRAME=<frame>[,...] records every composition blit for selected frames. */
void blt_trace_log(const BltTrace *trace);
void blt_trace_backdrop(const BltTrace *trace, const BackdropBlit *blit);

#endif
