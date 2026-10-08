#include "debug_socket.h"
#include <stdio.h>

/* Unix-domain debug transport is currently supported by macOS and Linux hosts.
 * Windows still runs the same core debug protocol tests without a live socket. */
bool jelli_sdl_debug_open(const char *path)
{
    if (path != NULL) {
        fprintf(stderr, "Live debug sockets are not yet supported by the Windows host\n");
        return false;
    }
    return true;
}

bool jelli_sdl_debug_poll(JelliPetEngine *engine)
{
    (void)engine;
    return false;
}

void jelli_sdl_debug_close(void) {}
