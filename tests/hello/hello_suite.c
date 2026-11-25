/* hello_suite - per-routine suite for the hello C (harness/suite.h). */
#include "suite.h"

extern const nmd_game hello_game;

int main(int argc, char **argv)
{
    return nmd_suite_main(argc, argv, &hello_game);
}
