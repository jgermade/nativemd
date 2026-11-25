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
