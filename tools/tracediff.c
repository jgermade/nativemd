/*
 * nmd-tracediff - compare two bus traces (docs/trace-format.md).
 *
 * Usage: nmd-tracediff [--context N] EXPECTED ACTUAL
 *
 * Comment lines ('#') are ignored. Reports the first divergent access with
 * the preceding N accesses of EXPECTED as context.
 * Exit status: 0 identical, 1 different, 2 usage or I/O error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compare.h"

int main(int argc, char **argv)
{
    unsigned context = 5;
    int argi = 1;
    if (argi + 1 < argc && strcmp(argv[argi], "--context") == 0) {
        context = (unsigned)strtoul(argv[argi + 1], NULL, 10);
        argi += 2;
    }
    if (argc - argi != 2) {
        fprintf(stderr, "usage: %s [--context N] EXPECTED ACTUAL\n", argv[0]);
        return 2;
    }
    FILE *a = fopen(argv[argi], "r"), *b = fopen(argv[argi + 1], "r");
    if (!a || !b) {
        perror(!a ? argv[argi] : argv[argi + 1]);
        return 2;
    }
    int r = nmd_trace_compare(a, argv[argi], b, argv[argi + 1], context, stdout);
    fclose(a);
    fclose(b);
    return r;
}
