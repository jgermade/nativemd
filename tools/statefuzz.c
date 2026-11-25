/*
 * nmd-statefuzz - derive input states for a routine suite from a captured one
 * (docs/harness.md, "Fuzzing"). Captured states show the inputs a routine
 * really gets while playing; fuzzing widens them within valid ranges.
 *
 * Usage: nmd-statefuzz BASE.nmds --out DIR [--name NAME] [--count N] [--seed S]
 *                      [--sweep FIELD=LO-HI] [--rand FIELD=LO-HI]... [--value FIELD=V]...
 *
 *   FIELD   a register part (d0..d7, a0..a7 with .b/.w/.l, default .l), a RAM
 *           location written as ADDR.b/.w/.l (hex 24-bit address, e.g. FFF636.l),
 *           or RAM relative to an address register as (aN+OFF).b/.w/.l, e.g.
 *           (a0+12).w for the field at offset $12 of the object a0 points to
 *           (the register's value in the generated state is used)
 *   LO-HI   hex range; LO-HI:STEP restricts values to LO, LO+STEP, ... (e.g. even)
 *   --sweep one state per value of the range (at most 65536); other fields are
 *           randomised per state as given by --rand
 *   --rand  uniform random value of the range for each generated state
 *   --value fixed value in every generated state
 *   --count number of states when there is no --sweep (default 64)
 *   --seed  PRNG seed (default 1): runs are reproducible
 *
 * Only the selected part of a register changes (".w" keeps the high word), the
 * same way the routine's input would be loaded by its callers.
 * Output: DIR/NAME.NNNNN.nmds (NAME defaults to "fuzz").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"

typedef struct field {
    bool     ram;
    int      reg;      /* register field: 0-7 d, 8-15 a; RAM field: base aN or -1 */
    uint32_t addr;     /* RAM address, or offset from the base register */
    unsigned bytes;    /* 1, 2 or 4 */
    uint32_t lo, hi, step;
} field;

static uint64_t rng = 1;

static uint32_t next_rand(void)
{
    /* xorshift64* */
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t)((rng * 0x2545F4914F6CDD1DULL) >> 32);
}

static uint32_t rand_in(uint32_t lo, uint32_t hi)
{
    uint64_t span = (uint64_t)hi - lo + 1;
    return lo + (uint32_t)(((uint64_t)next_rand() * span) >> 32);
}

static bool parse_field(const char *spec, field *f, bool want_range)
{
    char name[32];
    const char *eq = strrchr(spec, '=');
    if (!eq || (size_t)(eq - spec) >= sizeof name) return false;
    memcpy(name, spec, (size_t)(eq - spec));
    name[eq - spec] = 0;

    char *dot = strrchr(name, '.');
    const char *paren = strrchr(name, ')');
    f->bytes = 4;
    if (dot && (!paren || dot > paren)) {
        if (!strcmp(dot, ".b")) f->bytes = 1;
        else if (!strcmp(dot, ".w")) f->bytes = 2;
        else if (!strcmp(dot, ".l")) f->bytes = 4;
        else return false;
        *dot = 0;
    }
    if (name[0] == '(') {                                   /* (aN+OFF) */
        unsigned n;
        unsigned long off;
        char close;
        if (sscanf(name, "(a%u+%lx%c", &n, &off, &close) != 3 || n > 7 || close != ')') return false;
        f->ram = true;
        f->reg = 8 + (int)n;
        f->addr = (uint32_t)off;
    } else if ((name[0] == 'd' || name[0] == 'a') && name[1] >= '0' && name[1] <= '7' && !name[2]) {
        f->ram = false;
        f->reg = (name[0] == 'a' ? 8 : 0) + (name[1] - '0');
    } else {
        char *end;
        f->addr = (uint32_t)strtoul(name, &end, 16) & 0xFFFFFF;
        if (*end || f->addr < 0xE00000) return false;
        f->ram = true;
        f->reg = -1;
    }
    unsigned long lo, hi, step = 1;
    if (want_range) {
        int k = sscanf(eq + 1, "%lx-%lx:%lx", &lo, &hi, &step);
        if (k < 2 || lo > hi || step == 0) return false;
    } else {
        if (sscanf(eq + 1, "%lx", &lo) != 1) return false;
        hi = lo;
    }
    uint64_t max = f->bytes == 4 ? 0xFFFFFFFFull : (1ull << (8 * f->bytes)) - 1;
    if (hi > max) return false;
    f->lo = (uint32_t)lo;
    f->hi = (uint32_t)hi;
    f->step = (uint32_t)step;
    return true;
}

static uint64_t field_values(const field *f) { return ((uint64_t)f->hi - f->lo) / f->step + 1; }

static uint32_t field_value(const field *f, uint64_t k) { return f->lo + (uint32_t)(k * f->step); }

static void apply(nmd_state_image *s, const field *f, uint32_t v)
{
    if (f->ram) {
        uint32_t a = (f->reg >= 8 ? s->regs.a[f->reg - 8] + f->addr : f->addr) & 0xFFFF;
        for (unsigned i = 0; i < f->bytes; i++)
            s->ram[(a + i) & 0xFFFF] = (uint8_t)(v >> (8 * (f->bytes - 1 - i)));
        return;
    }
    uint32_t *r = f->reg < 8 ? &s->regs.d[f->reg] : &s->regs.a[f->reg - 8];
    uint32_t mask = f->bytes == 4 ? 0xFFFFFFFFu : (1u << (8 * f->bytes)) - 1;
    *r = (*r & ~mask) | (v & mask);
}

int main(int argc, char **argv)
{
    const char *base = NULL, *out = NULL, *name = "fuzz";
    unsigned count = 64;
    field sweep;
    bool have_sweep = false;
    field rnd[16], fixed[16];
    int nrnd = 0, nfixed = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--out") && v)   { out = v; i++; }
        else if (!strcmp(a, "--name") && v)  { name = v; i++; }
        else if (!strcmp(a, "--count") && v) { count = (unsigned)strtoul(v, NULL, 10); i++; }
        else if (!strcmp(a, "--seed") && v)  { rng = strtoull(v, NULL, 10) | 1; i++; }
        else if (!strcmp(a, "--sweep") && v) {
            if (have_sweep || !parse_field(v, &sweep, true)) { fprintf(stderr, "bad --sweep %s\n", v); return 2; }
            have_sweep = true;
            i++;
        } else if (!strcmp(a, "--rand") && v) {
            if (nrnd >= 16 || !parse_field(v, &rnd[nrnd], true)) { fprintf(stderr, "bad --rand %s\n", v); return 2; }
            nrnd++;
            i++;
        } else if (!strcmp(a, "--value") && v) {
            if (nfixed >= 16 || !parse_field(v, &fixed[nfixed], false)) { fprintf(stderr, "bad --value %s\n", v); return 2; }
            nfixed++;
            i++;
        } else if (a[0] != '-' && !base) base = a;
        else { fprintf(stderr, "nmd-statefuzz: bad argument '%s' (see statefuzz.c)\n", a); return 2; }
    }
    if (!base || !out) {
        fprintf(stderr, "usage: nmd-statefuzz BASE.nmds --out DIR [--name NAME] [--count N] [--seed S] "
                        "[--sweep FIELD=LO-HI] [--rand FIELD=LO-HI]... [--value FIELD=V]...\n");
        return 2;
    }
    if (have_sweep && field_values(&sweep) > 65536) { fprintf(stderr, "sweep too large\n"); return 2; }

    nmd_state_image *s0 = malloc(sizeof *s0), *s = malloc(sizeof *s);
    if (!s0 || !s) return 2;
    if (!nmd_state_read(base, s0)) { fprintf(stderr, "%s: not a state file\n", base); return 2; }

    uint64_t n = have_sweep ? field_values(&sweep) : count;
    char path[1024];
    for (uint64_t k = 0; k < n; k++) {
        *s = *s0;
        /* Registers first, so (aN+OFF) fields use the generated register values. */
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < nfixed; i++)
                if (fixed[i].ram == (pass == 1)) apply(s, &fixed[i], fixed[i].lo);
            for (int i = 0; i < nrnd; i++)
                if (rnd[i].ram == (pass == 1))
                    apply(s, &rnd[i], field_value(&rnd[i], rand_in(0, (uint32_t)(field_values(&rnd[i]) - 1))));
            if (have_sweep && sweep.ram == (pass == 1)) apply(s, &sweep, field_value(&sweep, k));
        }
        snprintf(path, sizeof path, "%s/%s.%05llu.nmds", out, name, (unsigned long long)k);
        if (!nmd_state_write(path, s)) { perror(path); return 2; }
    }
    printf("nmd-statefuzz: %llu state(s) in %s\n", (unsigned long long)n, out);
    free(s0);
    free(s);
    return 0;
}
