/*
 * sdl_host.h - the desktop host (SDL2): a window with the engine's picture, keyboard and
 * game controllers as pad 1, frame pacing at the video rate. Optional: built only when
 * SDL2 is found. Everything platform-specific stays here, outside the engine.
 *
 * Keys: arrows = D-pad, A / S / D = A / B / C, Z / X / C = X / Y / Z, Enter = Start,
 * Alt+Enter (Cmd+Enter on macOS) = fullscreen, P = pause, Tab (held) = fast forward,
 * Esc = quit. X / Y / Z / Mode reach games once io.c has the 6-button protocol.
 * Controllers: D-pad or left stick, A / B / X = A / B / C, LB / Y / RB = X / Y / Z,
 * Back = Mode, Start.
 */
#ifndef NMD_SDL_HOST_H
#define NMD_SDL_HOST_H

#include <stdbool.h>

#include "nativemd/nativemd.h"

typedef struct nmd_sdl nmd_sdl;

/* Opens the window (scale: window size in multiples of 320x224). NULL on failure
 * (message on stderr). */
nmd_sdl *nmd_sdl_open(const char *title, int scale, bool fullscreen, nmd_video video);
nmd_host nmd_sdl_host(nmd_sdl *s);        /* present_frame, poll_pad, time_us */
bool     nmd_sdl_quit_requested(const nmd_sdl *s);
void     nmd_sdl_close(nmd_sdl *s);

#endif
