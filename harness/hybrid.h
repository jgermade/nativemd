/*
 * hybrid.h - run a ROM on the engine in hybrid mode (docs/harness.md,
 * "Hybrid execution"). A game's runner calls nmd_hybrid_main() with its
 * routine table: routines with C run as C, all others as original code.
 * With an empty table this is nmd-hybrid: the starting point of a
 * decompilation. Comparing the trace of a run with some C against a run
 * with none checks the C on the engine's own timing.
 *
 * Usage: PROGRAM ROM --frames N --idle START:END [--sym FILE] [--input FILE]
 *                [--trace FILE] [--state-out FILE] [--max-cycles N]
 */
#ifndef NMD_HARNESS_HYBRID_H
#define NMD_HARNESS_HYBRID_H

#include "nativemd/nativemd.h"

int nmd_hybrid_main(int argc, char **argv, const nmd_game *game);

#endif
