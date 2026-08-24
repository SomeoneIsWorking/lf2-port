/* Native process entry for the port. The PE CRT and LF2's WinMain are deliberately not
 * entered: this module owns construction, synchronous data loading, first presentation,
 * music handoff, and the frame loop in that order. */
#ifndef LF2_PORT_ENTRY_H
#define LF2_PORT_ENTRY_H

int port_entry_run(void);

#endif
