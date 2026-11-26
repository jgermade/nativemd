/* suite.c - per-routine verification suite (suite.h). */
#define _POSIX_C_SOURCE 200809L
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suite.h"
#include "compare.h"
#include "oracle/oracle.h"

static jmp_buf fatal_jmp;
static char    fatal_msg[512];

static void on_fatal(const char *msg)
{
    snprintf(fatal_msg, sizeof fatal_msg, "%s", msg);
    longjmp(fatal_jmp, 1);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

typedef struct run {
    char           *trace;
    size_t          trace_len;
    uint32_t        min_sp;     /* oracle run: lowest A7 (its stack differs from the C's) */
    uint64_t        rom_writes; /* ignored ROM writes (they are also in the trace) */
    nmd_state_image state;
} run;

/* Ignored ROM writes over all states, per side (the engine's table is per nmd_init). */
typedef struct rom_tally {
    nmd_rom_write_site sites[NMD_MAX_ROM_WRITE_SITES];
    const char        *first_state[NMD_MAX_ROM_WRITE_SITES];
    size_t             n;
    uint64_t           other, total, states;
} rom_tally;

static void tally_add(rom_tally *t, const char *state)
{
    const nmd_rom_write_site *s;
    uint64_t other;
    size_t n = nmd_rom_write_sites(&s, &other);
    if (!nmd_rom_write_total()) return;
    t->states++;
    t->total += nmd_rom_write_total();
    t->other += other;
    for (size_t i = 0; i < n; i++) {
        size_t j = 0;
        while (j < t->n && t->sites[j].addr != s[i].addr) j++;
        if (j < t->n) { t->sites[j].count += s[i].count; continue; }
        if (t->n == NMD_MAX_ROM_WRITE_SITES) { t->other += s[i].count; continue; }
        t->sites[t->n] = s[i];
        t->first_state[t->n++] = state;
    }
}

static void tally_print(const rom_tally *t, const char *who)
{
    if (!t->total) return;
    printf("%s: %llu ignored ROM write(s) in %llu state(s), at %zu address(es)%s\n", who,
           (unsigned long long)t->total, (unsigned long long)t->states, t->n, t->other ? " and more" : "");
    for (size_t i = 0; i < t->n; i++) {
        const nmd_rom_write_site *r = &t->sites[i];
        printf("  %06X x%llu, first write%u %0*X from ", r->addr, (unsigned long long)r->count,
               r->first_bits, (int)r->first_bits / 4, r->first_value);
        if (!r->first_known)      printf("unknown origin");
        else if (r->first_from_c) printf("C routine %06X", r->first_origin);
        else                      printf("PC %06X", r->first_origin);
        printf(" in %s\n", t->first_state[i]);
    }
    if (t->other) printf("  %llu more write(s) beyond the first %d addresses\n",
                         (unsigned long long)t->other, NMD_MAX_ROM_WRITE_SITES);
}

typedef struct suite {
    const nmd_game *game;
    const uint8_t  *rom;
    size_t          rom_size;
    uint32_t        addr;
    bool            irq, replay, verbose, hybrid;
    uint64_t        max_cycles; /* --max-cycles N: per oracle run (and hybrid slice); default 10M */
    const char     *keep;
    const char     *coverage;   /* --coverage FILE: original code the oracle runs, all states */
    const char     *syms[4];
    int             nsyms;
    nmd_cmp_opts    cmp;
    nmd_rom_writes  rom_writes;
    rom_tally      *rom_oracle, *rom_c;
} suite;

static const nmd_game no_game = { .name = "oracle" };

/* volatile: modified between setjmp and a possible longjmp */
static bool run_oracle(const suite *s, const char *state, run *out)
{
    nmd_config cfg = { .video = NMD_VIDEO_NTSC, .rom_writes = s->rom_writes };
    FILE *volatile mem = NULL;
    if (setjmp(fatal_jmp)) {
        nmd_trace_open(NULL);
        if (mem) fclose(mem);
        printf("  oracle: fatal: %s\n", fatal_msg);
        tally_add(s->rom_oracle, state);
        return false;
    }
    if (!nmd_init(&cfg, &no_game, s->rom, s->rom_size)) return false;
    nmd_set_fatal_handler(on_fatal);
    oracle_setup();
    if (!nmd_state_load(state)) { printf("  cannot load state\n"); return false; }
    mem = open_memstream(&out->trace, &out->trace_len);
    nmd_trace_open(mem);
    oracle_reset_min_sp();
    bool ok = oracle_call(s->addr, s->irq, s->max_cycles);
    out->min_sp = oracle_min_sp();
    out->rom_writes = nmd_rom_write_total();
    tally_add(s->rom_oracle, state);
    nmd_trace_open(NULL);
    fclose(mem);
    mem = NULL;
    nmd_state_capture(&out->state);
    return ok;
}

static bool run_candidate(const suite *s, const char *state, const run *expected, run *out)
{
    nmd_config cfg = { .video = NMD_VIDEO_NTSC, .rom_writes = s->rom_writes };
    FILE *volatile mem = NULL;
    if (setjmp(fatal_jmp)) {
        nmd_trace_open(NULL);
        nmd_replay_close();
        if (mem) fclose(mem);
        printf("  C: fatal: %s\n", fatal_msg);
        tally_add(s->rom_c, state);
        return false;
    }
    if (!nmd_init(&cfg, s->game, s->rom, s->rom_size)) return false;
    nmd_set_fatal_handler(on_fatal);
    nmd_set_missing_routine_handler(NULL);
    if (s->hybrid) {
        /* Routines the one under test calls but that have no C yet run as original code. */
        oracle_setup();
        oracle_install_hybrid(s->max_cycles);
    }
    if (!nmd_state_load(state)) { printf("  cannot load state\n"); return false; }
    if (s->replay) {
        FILE *rec = fmemopen(expected->trace, expected->trace_len ? expected->trace_len : 1, "r");
        bool opened = rec && nmd_replay_open(rec);
        if (rec) fclose(rec);
        if (!opened) { printf("  cannot open replay\n"); return false; }
    }
    mem = open_memstream(&out->trace, &out->trace_len);
    nmd_trace_open(mem);
    if (s->irq) {
        /* As the engine takes an interrupt (machine.c take_interrupt): M68K is saved
         * around the handler and restored after it, like the 68000's exception frame and
         * RTE (the oracle side returns through its RTE). The state was captured at the
         * handler's entry, so SR already holds the handler's mask. */
        m68k_regs saved = M68K;
        call_routine(s->addr);
        M68K = saved;
    } else {
        call_routine(s->addr);
    }
    nmd_trace_open(NULL);
    fclose(mem);
    mem = NULL;
    out->rom_writes = nmd_rom_write_total();
    tally_add(s->rom_c, state);
    if (s->replay && nmd_replay_remaining()) {
        printf("  C made %zu fewer device reads than the oracle\n", nmd_replay_remaining());
        nmd_replay_close();
        return false;
    }
    nmd_replay_close();
    nmd_state_capture(&out->state);
    return true;
}

static void keep_traces(const suite *s, const char *state, const run *e, const run *a)
{
    if (!s->keep) return;
    const char *base = strrchr(state, '/');
    base = base ? base + 1 : state;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.expected.trace", s->keep, base);
    FILE *f = fopen(path, "w");
    if (f) { fwrite(e->trace, 1, e->trace_len, f); fclose(f); }
    snprintf(path, sizeof path, "%s/%s.actual.trace", s->keep, base);
    f = fopen(path, "w");
    if (f) { fwrite(a->trace, 1, a->trace_len, f); fclose(f); }
}

static bool check_state(const suite *s, const char *state)
{
    run *e = calloc(1, sizeof *e), *a = calloc(1, sizeof *a);
    bool pass = false;
    if (!e || !a) goto done;
    printf("%s\n", state);
    if (!run_oracle(s, state, e)) goto done;
    if (!run_candidate(s, state, e, a)) goto done;
    if (e->rom_writes || a->rom_writes)
        printf("  ignored ROM writes: oracle %llu, C %llu\n",
               (unsigned long long)e->rom_writes, (unsigned long long)a->rom_writes);

    FILE *fe = fmemopen(e->trace, e->trace_len ? e->trace_len : 1, "r");
    FILE *fa = fmemopen(a->trace, a->trace_len ? a->trace_len : 1, "r");
    if (!e->trace_len) { fclose(fe); fe = fopen("/dev/null", "r"); }
    if (!a->trace_len) { fclose(fa); fa = fopen("/dev/null", "r"); }
    int t = nmd_trace_compare(fe, "oracle", fa, "C", 5, stdout);
    fclose(fe);
    fclose(fa);
    /* The oracle's return frame and pushes live below the state's A7: not compared. */
    nmd_cmp_opts cmp = s->cmp;
    uint32_t a7 = e->state.regs.a[7] & 0xFFFFFF;
    uint32_t lo = e->min_sp < a7 ? e->min_sp : a7;
    char range[32];
    snprintf(range, sizeof range, "%X-%X", lo, (a7 - 1) & 0xFFFFFF);
    if (lo < a7) nmd_cmp_ignore_ram(&cmp, range);
    int d = nmd_state_compare(&e->state, &a->state, &cmp, stdout);
    pass = t == 0 && d == 0;
    if (!pass) keep_traces(s, state, e, a);
done:
    printf("  %s\n", pass ? "PASS" : "FAIL");
    if (e) free(e->trace);
    if (a) free(a->trace);
    free(e);
    free(a);
    return pass;
}

int nmd_suite_main(int argc, char **argv, const nmd_game *game)
{
    suite s = { .game = game, .max_cycles = 10000000 };
    const char *rom_path = NULL, *call = NULL;
    /* every non-option argument is a state: at most argc of them (the command line is the
     * only limit; the OS refuses a too long one loudly) */
    const char **states = malloc((size_t)argc * sizeof *states);
    if (!states) { fprintf(stderr, "out of memory\n"); return 2; }
    int nstates = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--call") && v)       { call = v; i++; }
        else if (!strcmp(a, "--sym") && v)        {
            if (s.nsyms == 4) { fprintf(stderr, "more than 4 --sym\n"); return 2; }
            s.syms[s.nsyms++] = v; i++;
        }
        else if (!strcmp(a, "--regs") && v)       { snprintf(s.cmp.regs, sizeof s.cmp.regs, "%s", v); i++; }
        else if (!strcmp(a, "--ignore-ram") && v) {
            if (!nmd_cmp_ignore_ram(&s.cmp, v)) { fprintf(stderr, "bad --ignore-ram %s\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--keep") && v)       { s.keep = v; i++; }
        else if (!strcmp(a, "--coverage") && v)   { s.coverage = v; i++; }
        else if (!strcmp(a, "--rom-writes") && v) {
            if (!oracle_parse_rom_writes(v, &s.rom_writes)) { fprintf(stderr, "bad --rom-writes %s (ignore|fatal)\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--max-cycles") && v) {
            char *end;
            s.max_cycles = strtoull(v, &end, 0);
            if (*end || !s.max_cycles) { fprintf(stderr, "bad --max-cycles %s\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--irq"))             s.irq = true;
        else if (!strcmp(a, "--replay"))          s.replay = true;
        else if (!strcmp(a, "--hybrid"))          s.hybrid = true;
        else if (!strcmp(a, "-v"))                s.verbose = true;
        else if (a[0] == '-')                     { fprintf(stderr, "unknown option %s (see harness/suite.h)\n", a); return 2; }
        else if (!rom_path)                       rom_path = a;
        else                                      states[nstates++] = a;
    }
    if (!rom_path || !call || !nstates) {
        fprintf(stderr, "usage: %s ROM --call ADDR [--irq] [--sym FILE] [--regs LIST] "
                        "[--ignore-ram LO-HI]... [--replay] [--hybrid] [--keep DIR] [--coverage FILE] "
                        "[--rom-writes ignore|fatal] [--max-cycles N] STATE...\n", argv[0]);
        return 2;
    }
    uint8_t *rom = read_file(rom_path, &s.rom_size);
    if (!rom) { perror(rom_path); return 2; }
    s.rom = rom;

    /* Symbols and the default stack range need a machine: use a throwaway one. */
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    if (!nmd_init(&cfg, game, rom, s.rom_size)) return 2;
    for (int i = 0; i < s.nsyms; i++)
        if (!oracle_load_syms(s.syms[i])) { perror(s.syms[i]); return 2; }
    if (!oracle_resolve(call, &s.addr, NULL, NULL)) { fprintf(stderr, "unknown routine %s\n", call); return 2; }
    if (!find_routine(s.addr)) { fprintf(stderr, "no C for routine %06X in %s\n", s.addr, game->name); return 2; }
    if (s.coverage) oracle_enable_coverage();   /* kept across the per-state oracle runs */

    s.rom_oracle = calloc(1, sizeof *s.rom_oracle);
    s.rom_c = calloc(1, sizeof *s.rom_c);
    if (!s.rom_oracle || !s.rom_c) { fprintf(stderr, "out of memory\n"); return 2; }
    int passed = 0;
    for (int i = 0; i < nstates; i++) passed += check_state(&s, states[i]);
    free(states);
    if (s.coverage && !oracle_write_coverage(s.coverage)) perror(s.coverage);
    nmd_set_fatal_handler(NULL);
    nmd_shutdown();
    free(rom);
    tally_print(s.rom_oracle, "oracle");
    tally_print(s.rom_c, "C");
    free(s.rom_oracle);
    free(s.rom_c);
    printf("%s %06X: %d/%d states pass\n", game->name, s.addr, passed, nstates);
    return passed == nstates ? 0 : 1;
}
