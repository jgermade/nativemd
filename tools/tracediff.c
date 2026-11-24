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

#include "nativemd/trace.h"

typedef struct reader {
    FILE         *f;
    const char   *path;
    unsigned long lineno;
    char          text[256];
} reader;

/* Returns 1 with an access, 0 at EOF, -1 on a malformed line. */
static int next_access(reader *r, nmd_trace_line *out)
{
    while (fgets(r->text, sizeof r->text, r->f)) {
        r->lineno++;
        int k = nmd_trace_parse(r->text, out);
        if (k == 1) {
            r->text[strcspn(r->text, "\r\n")] = 0;
            return 1;
        }
        if (k < 0) {
            r->text[strcspn(r->text, "\r\n")] = 0;
            fprintf(stderr, "%s:%lu: malformed trace line: %s\n", r->path, r->lineno, r->text);
            return -1;
        }
    }
    return 0;
}

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
    reader a = { .path = argv[argi] }, b = { .path = argv[argi + 1] };
    a.f = fopen(a.path, "r");
    b.f = fopen(b.path, "r");
    if (!a.f || !b.f) {
        perror(!a.f ? a.path : b.path);
        return 2;
    }

    char (*ring)[256] = calloc(context ? context : 1, 256);
    unsigned long n = 0;
    for (;;) {
        nmd_trace_line la, lb;
        int ka = next_access(&a, &la), kb = next_access(&b, &lb);
        if (ka < 0 || kb < 0) return 2;
        if (ka == 0 && kb == 0) {
            printf("traces match (%lu accesses)\n", n);
            return 0;
        }
        bool same = ka == 1 && kb == 1 && la.bus == lb.bus && la.write == lb.write &&
                    la.bits == lb.bits && la.addr == lb.addr && la.value == lb.value;
        if (!same) {
            printf("traces diverge at access %lu\n", n + 1);
            unsigned shown = n < context ? (unsigned)n : context;
            for (unsigned i = shown; i > 0; i--)
                printf("    %s\n", ring[(n - i) % (context ? context : 1)]);
            printf("  expected (%s:%lu): %s\n", a.path, a.lineno, ka ? a.text : "<end of trace>");
            printf("  actual   (%s:%lu): %s\n", b.path, b.lineno, kb ? b.text : "<end of trace>");
            return 1;
        }
        if (context) snprintf(ring[n % context], 256, "%s", a.text);
        n++;
    }
}
