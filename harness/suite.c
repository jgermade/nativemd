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
    nmd_state_image state;
} run;

typedef struct suite {
    const nmd_game *game;
    const uint8_t  *rom;
    size_t          rom_size;
    uint32_t        addr;
    bool            irq, replay, verbose;
    const char     *keep;
    const char     *syms[4];
    int             nsyms;
    nmd_cmp_opts    cmp;
} suite;

static const nmd_game no_game = { .name = "oracle" };

/* volatile: modified between setjmp and a possible longjmp */
static bool run_oracle(const suite *s, const char *state, run *out)
{
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    FILE *volatile mem = NULL;
    if (setjmp(fatal_jmp)) {
        nmd_trace_open(NULL);
        if (mem) fclose(mem);
        printf("  oracle: fatal: %s\n", fatal_msg);
        return false;
    }
    if (!nmd_init(&cfg, &no_game, s->rom, s->rom_size)) return false;
    nmd_set_fatal_handler(on_fatal);
    oracle_setup();
    if (!nmd_state_load(state)) { printf("  cannot load state\n"); return false; }
    mem = open_memstream(&out->trace, &out->trace_len);
    nmd_trace_open(mem);
    bool ok = oracle_call(s->addr, s->irq, 10000000);
    nmd_trace_open(NULL);
    fclose(mem);
    mem = NULL;
    nmd_state_capture(&out->state);
    return ok;
}

static bool run_candidate(const suite *s, const char *state, const run *expected, run *out)
{
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    FILE *volatile mem = NULL;
    if (setjmp(fatal_jmp)) {
        nmd_trace_open(NULL);
        nmd_replay_close();
        if (mem) fclose(mem);
        printf("  C: fatal: %s\n", fatal_msg);
        return false;
    }
    if (!nmd_init(&cfg, s->game, s->rom, s->rom_size)) return false;
    nmd_set_fatal_handler(on_fatal);
    nmd_set_missing_routine_handler(NULL);
    if (!nmd_state_load(state)) { printf("  cannot load state\n"); return false; }
    if (s->replay) {
        FILE *rec = fmemopen(expected->trace, expected->trace_len ? expected->trace_len : 1, "r");
        bool opened = rec && nmd_replay_open(rec);
        if (rec) fclose(rec);
        if (!opened) { printf("  cannot open replay\n"); return false; }
    }
    mem = open_memstream(&out->trace, &out->trace_len);
    nmd_trace_open(mem);
    call_routine(s->addr);
    nmd_trace_open(NULL);
    fclose(mem);
    mem = NULL;
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

    FILE *fe = fmemopen(e->trace, e->trace_len ? e->trace_len : 1, "r");
    FILE *fa = fmemopen(a->trace, a->trace_len ? a->trace_len : 1, "r");
    if (!e->trace_len) { fclose(fe); fe = fopen("/dev/null", "r"); }
    if (!a->trace_len) { fclose(fa); fa = fopen("/dev/null", "r"); }
    int t = nmd_trace_compare(fe, "oracle", fa, "C", 5, stdout);
    fclose(fe);
    fclose(fa);
    int d = nmd_state_compare(&e->state, &a->state, &s->cmp, stdout);
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
    suite s = { .game = game };
    const char *rom_path = NULL, *call = NULL;
    const char *states[4096];
    int nstates = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--call") && v)       { call = v; i++; }
        else if (!strcmp(a, "--sym") && v)        { if (s.nsyms < 4) s.syms[s.nsyms++] = v; i++; }
        else if (!strcmp(a, "--regs") && v)       { snprintf(s.cmp.regs, sizeof s.cmp.regs, "%s", v); i++; }
        else if (!strcmp(a, "--ignore-ram") && v) {
            if (!nmd_cmp_ignore_ram(&s.cmp, v)) { fprintf(stderr, "bad --ignore-ram %s\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--keep") && v)       { s.keep = v; i++; }
        else if (!strcmp(a, "--irq"))             s.irq = true;
        else if (!strcmp(a, "--replay"))          s.replay = true;
        else if (!strcmp(a, "-v"))                s.verbose = true;
        else if (a[0] == '-')                     { fprintf(stderr, "unknown option %s (see harness/suite.h)\n", a); return 2; }
        else if (!rom_path)                       rom_path = a;
        else if (nstates < 4096)                  states[nstates++] = a;
    }
    if (!rom_path || !call || !nstates) {
        fprintf(stderr, "usage: %s ROM --call ADDR [--irq] [--sym FILE] [--regs LIST] "
                        "[--ignore-ram LO-HI]... [--replay] [--keep DIR] STATE...\n", argv[0]);
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
    uint32_t ssp = (uint32_t)rom[0] << 24 | (uint32_t)rom[1] << 16 | (uint32_t)rom[2] << 8 | rom[3];
    char stack[32];
    snprintf(stack, sizeof stack, "%X-%X", (ssp - 0x400) & 0xFFFFFF, (ssp - 1) & 0xFFFFFF);
    nmd_cmp_ignore_ram(&s.cmp, stack);

    int passed = 0;
    for (int i = 0; i < nstates; i++) passed += check_state(&s, states[i]);
    nmd_set_fatal_handler(NULL);
    nmd_shutdown();
    free(rom);
    printf("%s %06X: %d/%d states pass\n", game->name, s.addr, passed, nstates);
    return passed == nstates ? 0 : 1;
}
