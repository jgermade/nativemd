/*
 * trace.h - Bus access traces (see docs/trace-format.md).
 */
#ifndef NATIVEMD_TRACE_H
#define NATIVEMD_TRACE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmd_trace_bus {
    NMD_TRACE_M68K = 0,
    NMD_TRACE_Z80  = 1,
} nmd_trace_bus;

/* Start writing a trace to f (NULL stops). The engine does not close f. */
void nmd_trace_open(FILE *f);
bool nmd_trace_enabled(void);

/* Low-level emitters used by the engine; exposed for the harness. */
void nmd_trace_access(nmd_trace_bus bus, bool write, unsigned bits,
                      uint32_t addr, uint32_t value);
void nmd_trace_comment(const char *fmt, ...);

/*
 * Stack filter for the C's own accesses (0, the default: none). The oracle does not trace
 * accesses to the stack in use (between A7 and the initial SSP, and up to 4 bytes below
 * A7: docs/harness.md, "Trace filters"); with a top set, the engine drops the C's
 * accesses in the same window ([A7 - 4, top), A7 from M68K), so that original code and C
 * that write over the stack (a RAM clear at boot) give the same trace.
 */
void     nmd_trace_set_c_stack(uint32_t top);
uint32_t nmd_trace_c_stack(void);

/* Parsed trace line, for tools that compare traces. */
typedef struct nmd_trace_line {
    nmd_trace_bus bus;
    bool          write;
    unsigned      bits;  /* 8 or 16 */
    uint32_t      addr;
    uint32_t      value;
} nmd_trace_line;

/*
 * Parse one line. Returns 1 for an access, 0 for a comment or blank line,
 * -1 for a malformed line.
 */
int nmd_trace_parse(const char *line, nmd_trace_line *out);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_TRACE_H */
