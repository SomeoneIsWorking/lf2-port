#include "guest.h"
#include "com.h"
#include "hostwin.h"
#include "config.h"
#include "lf2_log.h"
#include "port_entry.h"

void import_stats_report(void);
void scan_prof_report(void);
void load_span_report(void);
void mix_dump_close(void);
void blt_stack_report(void);
void loadprof_report(void);

void ddraw_register(void);
void dsound_register(void);
void dshow_register(void);

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "game/lf2.exe";
    config_load();
    guest_init();
    ddraw_register();
    dsound_register();
    dshow_register();
    com_init();
    guest_load_image(exe);
    printf("image loaded, %d functions; using native port entry\n", g_nfuncs);
    /* The game exits through the CRT's exit(), not by returning from its entry point, so
     * teardown has to be an atexit hook -- calling it after dispatch() would never run.
     * Registered here at startup rather than lazily, so it is armed on every path. */
    atexit(lf2_log_flush);
    atexit(hostwin_shutdown);
    atexit(import_stats_report);
    atexit(scan_prof_report);
    atexit(load_span_report);
    atexit(mix_dump_close);
    atexit(blt_stack_report);
    atexit(loadprof_report);

    return port_entry_run();
}
