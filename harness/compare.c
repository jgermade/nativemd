/* compare.c - trace and state comparison. */
#include <stdlib.h>
#include <string.h>

#include "compare.h"

bool nmd_cmp_ignore_ram(nmd_cmp_opts *o, const char *range)
{
    unsigned lo, hi;
    if (o->nignore >= NMD_CMP_MAX_IGNORE || sscanf(range, "%x-%x", &lo, &hi) != 2 || lo > hi) return false;
    o->ignore_ram[o->nignore][0] = lo & 0xFFFFFF;
    o->ignore_ram[o->nignore][1] = hi & 0xFFFFFF;
    o->nignore++;
    return true;
}

/* ---- states ------------------------------------------------------------------ */

typedef struct ctx { FILE *report; int diffs; } ctx;

static void bytes(ctx *c, const char *name, const uint8_t *a, const uint8_t *b, size_t n,
                  uint32_t base, const nmd_cmp_opts *o)
{
    int shown = 0, region_diffs = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t addr = base + (uint32_t)i;
        if (a[i] == b[i]) continue;
        bool skip = false;
        for (int k = 0; o && k < o->nignore; k++)
            if (addr >= o->ignore_ram[k][0] && addr <= o->ignore_ram[k][1]) skip = true;
        if (skip) continue;
        c->diffs++;
        region_diffs++;
        if (c->report && shown++ < 8)
            fprintf(c->report, "  %-6s %06X: expected %02X, actual %02X\n", name, addr, a[i], b[i]);
    }
    if (c->report && region_diffs > 8)
        fprintf(c->report, "  %-6s ... %d differing bytes in total\n", name, region_diffs);
}

static void words(ctx *c, const char *name, const uint16_t *a, const uint16_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        c->diffs++;
        if (c->report) fprintf(c->report, "  %-6s [%zu]: expected %04X, actual %04X\n", name, i, a[i], b[i]);
    }
}

static void value(ctx *c, const char *name, uint32_t a, uint32_t b)
{
    if (a == b) return;
    c->diffs++;
    if (c->report) fprintf(c->report, "  %-16s expected %X, actual %X\n", name, a, b);
}

int nmd_state_compare(const nmd_state_image *e, const nmd_state_image *a, const nmd_cmp_opts *o, FILE *report)
{
    ctx c = { report, 0 };
    if (o && o->regs[0]) {
        char list[128];
        snprintf(list, sizeof list, "%s", o->regs);
        for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
            if ((tok[0] == 'd' || tok[0] == 'a') && tok[1] >= '0' && tok[1] <= '7' && !tok[2]) {
                int i = tok[1] - '0';
                value(&c, tok, tok[0] == 'd' ? e->regs.d[i] : e->regs.a[i],
                               tok[0] == 'd' ? a->regs.d[i] : a->regs.a[i]);
            } else if (!strcmp(tok, "sr")) {
                value(&c, "sr", e->regs.sr, a->regs.sr);
            } else if (!strcmp(tok, "ccr")) {
                value(&c, "ccr", e->regs.sr & 0x1F, a->regs.sr & 0x1F);
            } else {
                c.diffs++;
                if (report) fprintf(report, "  unknown register '%s'\n", tok);
            }
        }
    }
    bytes(&c, "RAM", e->ram, a->ram, sizeof e->ram, 0xFF0000, o);
    bytes(&c, "VRAM", e->vram, a->vram, sizeof e->vram, 0, NULL);
    words(&c, "CRAM", e->cram, a->cram, 64);
    words(&c, "VSRAM", e->vsram, a->vsram, 40);
    bytes(&c, "VDPREG", e->vdp_reg, a->vdp_reg, sizeof e->vdp_reg, 0, NULL);
    bytes(&c, "Z80RAM", e->z80_ram, a->z80_ram, sizeof e->z80_ram, 0, NULL);
    if (!o || !o->skip_latches) {
        value(&c, "vdp.code", e->vdp_code, a->vdp_code);
        value(&c, "vdp.addr", e->vdp_addr, a->vdp_addr);
        value(&c, "vdp.cmd_pending", e->vdp_cmd_pending, a->vdp_cmd_pending);
        value(&c, "vdp.fill_pending", e->vdp_fill_pending, a->vdp_fill_pending);
        value(&c, "vdp.in_vblank", e->vdp_in_vblank, a->vdp_in_vblank);
        value(&c, "vdp.vint_pending", e->vdp_vint_pending, a->vdp_vint_pending);
        value(&c, "vdp.hint_counter", (uint16_t)e->vdp_hint_counter, (uint16_t)a->vdp_hint_counter);
        bytes(&c, "IODATA", e->io_data, a->io_data, 3, 0, NULL);
        bytes(&c, "IOCTRL", e->io_ctrl, a->io_ctrl, 3, 0, NULL);
        value(&c, "z80.busreq", e->z80_busreq, a->z80_busreq);
        value(&c, "z80.reset", e->z80_reset, a->z80_reset);
        value(&c, "z80.bank", e->z80_bank, a->z80_bank);
        value(&c, "psg.last", e->psg_last, a->psg_last);
        bytes(&c, "YMADDR", e->ym_addr, a->ym_addr, 2, 0, NULL);
        bytes(&c, "YMREG", &e->ym_reg[0][0], &a->ym_reg[0][0], sizeof e->ym_reg, 0, NULL);
    }
    return c.diffs;
}

/* ---- traces ------------------------------------------------------------------ */

typedef struct reader {
    FILE         *f;
    const char   *name;
    unsigned long lineno;
    char          text[256];
} reader;

static int next_access(reader *r, nmd_trace_line *out, FILE *report)
{
    while (fgets(r->text, sizeof r->text, r->f)) {
        r->lineno++;
        int k = nmd_trace_parse(r->text, out);
        r->text[strcspn(r->text, "\r\n")] = 0;
        if (k == 1) return 1;
        if (k < 0) {
            if (report) fprintf(report, "%s:%lu: malformed trace line: %s\n", r->name, r->lineno, r->text);
            return -1;
        }
    }
    return 0;
}

int nmd_trace_compare(FILE *ef, const char *en, FILE *af, const char *an, unsigned context, FILE *report)
{
    reader a = { .f = ef, .name = en }, b = { .f = af, .name = an };
    unsigned ring_n = context ? context : 1;
    char (*ring)[256] = calloc(ring_n, 256);
    if (!ring) return 2;
    unsigned long n = 0;
    int result;
    for (;;) {
        nmd_trace_line la, lb;
        int ka = next_access(&a, &la, report), kb = next_access(&b, &lb, report);
        if (ka < 0 || kb < 0) { result = 2; break; }
        if (ka == 0 && kb == 0) {
            if (report) fprintf(report, "traces match (%lu accesses)\n", n);
            result = 0;
            break;
        }
        bool same = ka == 1 && kb == 1 && la.bus == lb.bus && la.write == lb.write &&
                    la.bits == lb.bits && la.addr == lb.addr && la.value == lb.value;
        if (!same) {
            if (report) {
                fprintf(report, "traces diverge at access %lu\n", n + 1);
                unsigned shown = n < context ? (unsigned)n : context;
                for (unsigned i = shown; i > 0; i--)
                    fprintf(report, "    %s\n", ring[(n - i) % ring_n]);
                fprintf(report, "  expected (%s:%lu): %s\n", en, a.lineno, ka ? a.text : "<end of trace>");
                fprintf(report, "  actual   (%s:%lu): %s\n", an, b.lineno, kb ? b.text : "<end of trace>");
            }
            result = 1;
            break;
        }
        if (context) snprintf(ring[n % ring_n], 256, "%s", a.text);
        n++;
    }
    free(ring);
    return result;
}
