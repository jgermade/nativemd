/* compare.h - trace and state comparison (docs/harness.md). */
#ifndef NMD_HARNESS_COMPARE_H
#define NMD_HARNESS_COMPARE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "nativemd/nativemd.h"

#define NMD_CMP_MAX_IGNORE 16

typedef struct nmd_cmp_opts {
    uint32_t ignore_ram[NMD_CMP_MAX_IGNORE][2]; /* inclusive 24-bit ranges */
    int      nignore;
    char     regs[128];                          /* "d0,a1,sr" or empty */
    bool     skip_latches;
} nmd_cmp_opts;

/* Adds "LO-HI" (hex) to the ignored RAM ranges. Returns false if malformed or full. */
bool nmd_cmp_ignore_ram(nmd_cmp_opts *o, const char *range);

/* Number of differences; each one is described on report (if not NULL). */
int nmd_state_compare(const nmd_state_image *expected, const nmd_state_image *actual,
                      const nmd_cmp_opts *o, FILE *report);

/*
 * Compare two traces access by access (comments ignored). Returns 0 if they
 * match, 1 if they differ, 2 on a malformed line. The first divergence and
 * `context` preceding accesses are written to report.
 */
int nmd_trace_compare(FILE *expected, const char *expected_name,
                      FILE *actual, const char *actual_name,
                      unsigned context, FILE *report);

#endif
