/* nmd-play: play a ROM with no C at all (every routine as original code). */
#include "play.h"

static const nmd_game no_c = { .name = "nmd-play" };

int main(int argc, char **argv) { return nmd_play_main(argc, argv, &no_c, NULL); }
