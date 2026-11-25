/*
 * nmd-oracle - command-line front end of the oracle (oracle.h).
 *
 * Modes:
 *   frames:  nmd-oracle ROM --frames N [options]
 *            Reset, run with NTSC line timing, stop right after the RTE of
 *            the N-th VINT handler (where the engine's nmd_run_frames(N) stops).
 *   routine: nmd-oracle ROM --state IN.nmds --call ADDR [--irq] [options]
 *            Load a state, call ADDR, stop when it returns.
 *
 * Options:
 *   --trace FILE           write the bus trace
 *   --state-out FILE       write the final state
 *   --input FILE           pad script: one line per frame, "P1 [P2]" in hex
 *   --sym FILE             symbols "ADDRESS NAME" (names usable below)
 *   --idle START[:END]     idle loop (END defaults to the next symbol)
 *   --stack LO-HI          fixed stack range whose accesses are not traced (default:
 *                          between the current A7 and the initial SSP)
 *   --coverage FILE        executed instruction addresses
 *   --capture ADDR         save a state each time PC reaches ADDR (repeatable)
 *   --capture-dir DIR      where captured states go (default .)
 *   --capture-max N        per address (default 16)
 *   --capture-every K      keep only every K-th hit of each address (default 1)
 *   --irq                  routine mode: ADDR ends in RTE (interrupt handler)
 *   --max-cycles N         routine mode limit (default 10000000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"
#include "oracle.h"

static void fatal_handler(const char *msg)
{
    fprintf(stderr, "nmd-oracle: fatal: %s\n", msg);
    exit(3);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static uint16_t (*load_input(const char *path, size_t *frames))[2]
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    uint16_t (*script)[2] = NULL;
    size_t n = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        unsigned p1 = 0, p2 = 0;
        if (sscanf(line, "%x %x", &p1, &p2) < 1) continue;
        script = realloc(script, (n + 1) * sizeof *script);
        script[n][0] = (uint16_t)p1;
        script[n][1] = (uint16_t)p2;
        n++;
    }
    fclose(f);
    *frames = n;
    return script;
}

static uint32_t need_addr(const char *text)
{
    uint32_t a;
    if (!oracle_resolve(text, &a, NULL, NULL)) {
        fprintf(stderr, "nmd-oracle: unknown symbol or address '%s'\n", text);
        exit(2);
    }
    return a;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *trace_path = NULL, *state_in = NULL, *state_out = NULL;
    const char *idle = NULL, *stack = NULL, *cov_path = NULL, *call = NULL, *input = NULL;
    const char *captures[16];
    int ncaptures = 0;
    const char *capture_dir = ".";
    unsigned capture_max = 16, capture_every = 1;
    uint64_t frames = 0, max_cycles = 10000000;
    bool irq = false;
    const char *syms[4];
    int nsyms = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define OPT(name) (strcmp(a, name) == 0 && v && (i++, 1))
        if      (OPT("--frames"))      frames = strtoull(v, NULL, 10);
        else if (OPT("--trace"))       trace_path = v;
        else if (OPT("--state"))       state_in = v;
        else if (OPT("--state-out"))   state_out = v;
        else if (OPT("--call"))        call = v;
        else if (OPT("--input"))       input = v;
        else if (OPT("--sym"))         { if (nsyms < 4) syms[nsyms++] = v; }
        else if (OPT("--idle"))        idle = v;
        else if (OPT("--stack"))       stack = v;
        else if (OPT("--coverage"))    cov_path = v;
        else if (OPT("--capture"))     { if (ncaptures < 16) captures[ncaptures++] = v; }
        else if (OPT("--capture-dir")) capture_dir = v;
        else if (OPT("--capture-max")) capture_max = (unsigned)strtoul(v, NULL, 10);
        else if (OPT("--capture-every")) capture_every = (unsigned)strtoul(v, NULL, 10);
        else if (OPT("--max-cycles"))  max_cycles = strtoull(v, NULL, 10);
        else if (strcmp(a, "--irq") == 0) irq = true;
        else if (a[0] != '-' && !rom_path) rom_path = a;
        else { fprintf(stderr, "nmd-oracle: bad argument '%s' (see oracle_main.c)\n", a); return 2; }
#undef OPT
    }
    if (!rom_path || (!frames && !call) || (frames && call) || (call && !state_in)) {
        fprintf(stderr, "usage: nmd-oracle ROM --frames N [opts]\n"
                        "       nmd-oracle ROM --state IN.nmds --call ADDR [--irq] [opts]\n");
        return 2;
    }

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    static const nmd_game no_game = { .name = "oracle" };
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    if (!nmd_init(&cfg, &no_game, rom, rom_size)) return 2;
    nmd_set_fatal_handler(fatal_handler);
    oracle_setup();
    for (int i = 0; i < nsyms; i++)
        if (!oracle_load_syms(syms[i])) { perror(syms[i]); return 2; }

    if (stack) {
        unsigned lo, hi;
        if (sscanf(stack, "%x-%x", &lo, &hi) != 2) { fprintf(stderr, "nmd-oracle: bad --stack\n"); return 2; }
        oracle_set_stack(lo, hi);
    }
    if (idle) {
        char lo[64], hi[64] = "";
        if (sscanf(idle, "%63[^:]:%63s", lo, hi) < 1) { fprintf(stderr, "nmd-oracle: bad --idle\n"); return 2; }
        uint32_t start, end, next;
        bool has_next;
        if (!oracle_resolve(lo, &start, &next, &has_next)) { fprintf(stderr, "nmd-oracle: unknown '%s'\n", lo); return 2; }
        if (hi[0]) end = need_addr(hi);
        else if (has_next) end = next;
        else { fprintf(stderr, "nmd-oracle: --idle %s needs an end\n", idle); return 2; }
        oracle_set_idle(start, end);
    }
    size_t pad_frames = 0;
    uint16_t (*pads)[2] = input ? load_input(input, &pad_frames) : NULL;
    oracle_set_pad_script((const uint16_t (*)[2])pads, pad_frames);
    if (cov_path) oracle_enable_coverage();
    oracle_set_capture(capture_dir, capture_max);
    oracle_set_capture_every(capture_every);
    for (int i = 0; i < ncaptures; i++)
        if (!oracle_add_capture(need_addr(captures[i]), captures[i])) { fprintf(stderr, "too many captures\n"); return 2; }

    if (state_in && !nmd_state_load(state_in)) { fprintf(stderr, "nmd-oracle: cannot load %s\n", state_in); return 2; }

    FILE *trace = NULL;
    if (trace_path && !(trace = fopen(trace_path, "w"))) { perror(trace_path); return 2; }
    nmd_trace_open(trace);
    bool ok = frames ? oracle_run_frames(frames) : oracle_call(need_addr(call), irq, max_cycles);
    nmd_trace_open(NULL);
    if (trace) fclose(trace);
    if (!ok) return 1;

    if (state_out && !nmd_state_save(state_out)) { perror(state_out); return 2; }
    if (cov_path && !oracle_write_coverage(cov_path)) { perror(cov_path); return 2; }
    uint64_t first_lag;
    uint64_t lags = frames ? oracle_lag_frames(&first_lag) : 0;
    if (lags)
        fprintf(stderr, "nmd-oracle: %llu lag frame(s), first at VINT %llu (marked '# lag' in the trace)\n",
                (unsigned long long)lags, (unsigned long long)first_lag);
    if (ncaptures) fprintf(stderr, "nmd-oracle: %u state(s) captured in %s\n", oracle_captured(), capture_dir);
    nmd_shutdown();
    free(rom);
    free(pads);
    return 0;
}
