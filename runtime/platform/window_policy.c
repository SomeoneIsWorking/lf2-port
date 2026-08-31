#include "window_policy.h"

#include <SDL3/SDL.h>

void window_policy_prepare(void)
{
#ifdef __ANDROID__
    /* SDLActivity supersedes the manifest orientation after SDL_CreateWindow. A resizable
     * window with no hint becomes FULL_USER, which re-enables portrait. Supplying the two
     * landscape orientations at SDL's actual ownership boundary keeps rotation available
     * without allowing the portrait layouts LF2 cannot present. */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
}
