/*
 * play.h - play a game interactively (docs/harness.md, "Playing"): the hybrid runner
 * (C where it exists, the original code on the oracle CPU elsewhere) with the desktop
 * host (host/sdl). Built only when SDL2 is found.
 *
 * Usage: PROGRAM ROM [--region jp|us|eu] [--pal] [--no-vsync] [--scale N] [--fullscreen]
 *                [--idle START:END] [--sym FILE] [--frames N] [--max-cycles N]
 *   --region  the console: jp (domestic, 60 Hz), us (overseas, 60 Hz; the default),
 *             eu (overseas, 50 Hz; also --pal). Sets the version register and the pacing.
 *   --idle    the game's VBlank wait loop (default: the runner's)
 *   --frames  stop after N frames (0, the default: until the window is closed)
 */
#ifndef NMD_HARNESS_PLAY_H
#define NMD_HARNESS_PLAY_H

#include "nativemd/nativemd.h"

int nmd_play_main(int argc, char **argv, const nmd_game *game, const char *default_idle);

#endif
