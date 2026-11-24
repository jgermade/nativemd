/*
 * nmd-statediff - compare two .nmds state files (docs/harness.md).
 *
 * Usage: nmd-statediff [--regs LIST] [--ignore-ram LO-HI]... [--no-latches] EXPECTED ACTUAL
 *
 *   --regs        comma-separated registers to compare (d0..d7, a0..a7, sr, ccr);
 *                 none by default, since C code need not keep values in them
 *   --ignore-ram  hex 24-bit range of 68000 RAM to skip (e.g. the stack)
 *   --no-latches  do not compare VDP / I/O / Z80 latches
 *
 * Exit status: 0 equal, 1 different, 2 error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compare.h"

int main(int argc, char **argv)
{
    nmd_cmp_opts o = {0};
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "--ignore-ram") && argi + 1 < argc) {
            if (!nmd_cmp_ignore_ram(&o, argv[argi + 1])) { fprintf(stderr, "bad range %s\n", argv[argi + 1]); return 2; }
            argi += 2;
        } else if (!strcmp(argv[argi], "--regs") && argi + 1 < argc) {
            snprintf(o.regs, sizeof o.regs, "%s", argv[argi + 1]);
            argi += 2;
        } else if (!strcmp(argv[argi], "--no-latches")) {
            o.skip_latches = true;
            argi++;
        } else {
            break;
        }
    }
    if (argc - argi != 2) {
        fprintf(stderr, "usage: %s [--regs LIST] [--ignore-ram LO-HI]... [--no-latches] EXPECTED ACTUAL\n", argv[0]);
        return 2;
    }
    nmd_state_image *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    if (!a || !b) return 2;
    if (!nmd_state_read(argv[argi], a))     { fprintf(stderr, "%s: not a state file\n", argv[argi]); return 2; }
    if (!nmd_state_read(argv[argi + 1], b)) { fprintf(stderr, "%s: not a state file\n", argv[argi + 1]); return 2; }
    int d = nmd_state_compare(a, b, &o, stdout);
    printf(d ? "states differ (%d)\n" : "states match\n", d);
    free(a);
    free(b);
    return d ? 1 : 0;
}
