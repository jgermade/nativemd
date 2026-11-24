/* nmd-hybrid - run a ROM with no C at all (hybrid.h). */
#include "hybrid.h"

int main(int argc, char **argv)
{
    static const nmd_game no_c = { .name = "nmd-hybrid (no C)" };
    return nmd_hybrid_main(argc, argv, &no_c);
}
