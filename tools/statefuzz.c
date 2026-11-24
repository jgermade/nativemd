/*
 * nmd-statefuzz - derive input states for a routine suite from a captured one
 * (docs/harness.md, "Fuzzing"). Captured states show the inputs a routine
 * really gets while playing; fuzzing widens them within valid ranges.
 *
 * Usage: nmd-statefuzz BASE.nmds --out DIR [--name NAME] [--count N] [--seed S]
 *                      [--sweep FIELD=LO-HI] [--rand FIELD=LO-HI]... [--value FIELD=V]...
 *
 *   FIELD   a register part (d0..d7, a0..a7 with .b/.w/.l, default .l) or a RAM
 *           location written as ADDR.b/.w/.l (hex 24-bit address, e.g. FFF636.l)
 *   --sweep one state per value of LO..HI (at most 65536); other fields are
 *           randomised per state as given by --rand
 *   --rand  uniform random value in LO..HI for each generated state
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
    int      reg;      /* 0-7 d, 8-15 a */
    uint32_t addr;
    unsigned bytes;    /* 1, 2 or 4 */
    uint32_t lo, hi;
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
    const char *eq = strchr(spec, '=');
    if (!eq || (size_t)(eq - spec) >= sizeof name) return false;
    memcpy(name, spec, (size_t)(eq - spec));
    name[eq - spec] = 0;

    char *dot = strchr(name, '.');
    f->bytes = 4;
    if (dot) {
        if (!strcmp(dot, ".b")) f->bytes = 1;
        else if (!strcmp(dot, ".w")) f->bytes = 2;
        else if (!strcmp(dot, ".l")) f->bytes = 4;
        else return false;
        *dot = 0;
    }
    if ((name[0] == 'd' || name[0] == 'a') && name[1] >= '0' && name[1] <= '7' && !name[2]) {
        f->ram = false;
        f->reg = (name[0] == 'a' ? 8 : 0) + (name[1] - '0');
    } else {
        char *end;
        f->addr = (uint32_t)strtoul(name, &end, 16) & 0xFFFFFF;
        if (*end || f->addr < 0xE00000) return false;
        f->ram = true;
    }
    unsigned long lo, hi;
    if (want_range) {
        if (sscanf(eq + 1, "%lx-%lx", &lo, &hi) != 2 || lo > hi) return false;
    } else {
        if (sscanf(eq + 1, "%lx", &lo) != 1) return false;
        hi = lo;
    }
    uint64_t max = f->bytes == 4 ? 0xFFFFFFFFull : (1ull << (8 * f->bytes)) - 1;
    if (hi > max) return false;
    f->lo = (uint32_t)lo;
    f->hi = (uint32_t)hi;
    return true;
}

static void apply(nmd_state_image *s, const field *f, uint32_t v)
{
    if (f->ram) {
        uint32_t a = f->addr & 0xFFFF;
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
    if (have_sweep && (uint64_t)sweep.hi - sweep.lo >= 65536) { fprintf(stderr, "sweep too large\n"); return 2; }

    nmd_state_image *s0 = malloc(sizeof *s0), *s = malloc(sizeof *s);
    if (!s0 || !s) return 2;
    if (!nmd_state_read(base, s0)) { fprintf(stderr, "%s: not a state file\n", base); return 2; }

    uint64_t n = have_sweep ? (uint64_t)sweep.hi - sweep.lo + 1 : count;
    char path[1024];
    for (uint64_t k = 0; k < n; k++) {
        *s = *s0;
        for (int i = 0; i < nfixed; i++) apply(s, &fixed[i], fixed[i].lo);
        for (int i = 0; i < nrnd; i++) apply(s, &rnd[i], rand_in(rnd[i].lo, rnd[i].hi));
        if (have_sweep) apply(s, &sweep, sweep.lo + (uint32_t)k);
        snprintf(path, sizeof path, "%s/%s.%05llu.nmds", out, name, (unsigned long long)k);
        if (!nmd_state_write(path, s)) { perror(path); return 2; }
    }
    printf("nmd-statefuzz: %llu state(s) in %s\n", (unsigned long long)n, out);
    free(s0);
    free(s);
    return 0;
}
