#include "guest.h"
#include "com.h"

void ddraw_register(void);
void dsound_register(void);
void dshow_register(void);

#include <stdio.h>

/* PE AddressOfEntryPoint + image base. */
enum { ENTRY = 0x445560 };

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "game/lf2.exe";
    guest_init();
    ddraw_register();
    dsound_register();
    dshow_register();
    com_init();
    guest_load_image(exe);
    printf("image loaded, %d functions, entering at %08x\n", g_nfuncs, ENTRY);
    dispatch(ENTRY);
    printf("returned from entry point\n");
    return 0;
}
