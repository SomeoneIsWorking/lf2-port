#ifndef LF2_ENGINE_VISIBILITY_PROBE_H
#define LF2_ENGINE_VISIBILITY_PROBE_H

struct SDL_Renderer;

/* LF2_VISIBILITY_PROBE=<arm>: exercise the shipping engine_draw visibility chain with
 * procedural host-owned art, then read selected output pixels back through SDL_Render. Arms
 * cover character occlusion, independent held-object casting, earlier/equal/later shadow
 * receiver depth, and a LEQUAL other-answer mutation; a normal run does nothing. */
void engine_visibility_probe_run(struct SDL_Renderer *renderer);

#endif
