/* trace.c - bus trace writer and parser (docs/trace-format.md). */
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static FILE *trace_file;

void nmd_trace_open(FILE *f) { trace_file = f; }
bool nmd_trace_enabled(void) { return trace_file != NULL; }

void nmd_trace_access(nmd_trace_bus bus, bool write, unsigned bits, uint32_t addr, uint32_t value)
{
    if (!trace_file) return;
    const char *prefix = bus == NMD_TRACE_Z80 ? "Z" : "";
    const char *arrow  = write ? "<-" : "->";
    if (bus == NMD_TRACE_Z80)
        fprintf(trace_file, "%s%c%u %04X %s %02X\n", prefix, write ? 'W' : 'R', bits,
                addr & 0xFFFF, arrow, value & 0xFF);
    else if (bits == 8)
        fprintf(trace_file, "%c8 %06X %s %02X\n", write ? 'W' : 'R', addr & 0xFFFFFF, arrow, value & 0xFF);
    else
        fprintf(trace_file, "%c16 %06X %s %04X\n", write ? 'W' : 'R', addr & 0xFFFFFF, arrow, value & 0xFFFF);
}

void nmd_trace_comment(const char *fmt, ...)
{
    if (!trace_file) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("# ", trace_file);
    vfprintf(trace_file, fmt, ap);
    fputc('\n', trace_file);
    va_end(ap);
}

static int parse_hex(const char **p, unsigned digits, uint32_t *out)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < digits; i++) {
        char c = (*p)[i];
        if (!isxdigit((unsigned char)c)) return -1;
        v = v << 4 | (uint32_t)(isdigit((unsigned char)c) ? c - '0' : (toupper((unsigned char)c) - 'A' + 10));
    }
    if (isxdigit((unsigned char)(*p)[digits])) return -1;
    *p += digits;
    *out = v;
    return 0;
}

int nmd_trace_parse(const char *line, nmd_trace_line *out)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') return 0;

    memset(out, 0, sizeof *out);
    out->bus = NMD_TRACE_M68K;
    if (*p == 'Z') { out->bus = NMD_TRACE_Z80; p++; }
    if (*p == 'R') out->write = false;
    else if (*p == 'W') out->write = true;
    else return -1;
    p++;
    if (p[0] == '8') { out->bits = 8; p += 1; }
    else if (p[0] == '1' && p[1] == '6') { out->bits = 16; p += 2; }
    else return -1;
    if (out->bus == NMD_TRACE_Z80 && out->bits != 8) return -1;
    if (*p++ != ' ') return -1;
    if (parse_hex(&p, out->bus == NMD_TRACE_Z80 ? 4 : 6, &out->addr)) return -1;
    if (strncmp(p, out->write ? " <- " : " -> ", 4) != 0) return -1;
    p += 4;
    if (parse_hex(&p, out->bits / 4, &out->value)) return -1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0' ? 1 : -1;
}
