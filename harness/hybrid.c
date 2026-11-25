/*
 * hybrid.c - run a ROM on the engine in hybrid mode (hybrid.h): routines
 * with C run as C, every other routine as original code on the oracle CPU,
 * scheduled by the engine's frame loop.
 *
 * Usage: PROGRAM ROM --frames N --idle START:END [--sym FILE] [--input FILE]
 *                   [--trace FILE] [--state-out FILE] [--max-cycles N]
 *
 *   --idle        the game's VBlank wait loop (required: it is where the
 *                 engine runs the rest of the frame)
 *   --input       pad script, one line per frame: "P1 [P2]" in hex (NMD_PAD_*)
 *   --max-cycles  limit between two waits before giving up (default 50000000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hybrid.h"
#include "oracle/oracle.h"

static void fatal_handler(const char *msg)
{
    fprintf(stderr, "hybrid: fatal: %s (frame %llu)\n", msg, (unsigned long long)nmd_frame_count());
    exit(3);
}

int nmd_hybrid_main(int argc, char **argv, const nmd_game *game)
{
    const char *rom_path = NULL, *idle = NULL, *input = NULL, *trace_path = NULL, *state_out = NULL;
    const char *syms[4];
    int nsyms = 0;
    uint64_t frames = 0, max_cycles = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--frames") && v)     { frames = strtoull(v, NULL, 10); i++; }
        else if (!strcmp(a, "--idle") && v)       { idle = v; i++; }
        else if (!strcmp(a, "--sym") && v)        { if (nsyms < 4) syms[nsyms++] = v; i++; }
        else if (!strcmp(a, "--input") && v)      { input = v; i++; }
        else if (!strcmp(a, "--trace") && v)      { trace_path = v; i++; }
        else if (!strcmp(a, "--state-out") && v)  { state_out = v; i++; }
        else if (!strcmp(a, "--max-cycles") && v) { max_cycles = strtoull(v, NULL, 10); i++; }
        else if (a[0] != '-' && !rom_path)        rom_path = a;
        else { fprintf(stderr, "nmd-hybrid: bad argument '%s' (see hybrid_main.c)\n", a); return 2; }
    }
    if (!rom_path || !frames || !idle) {
        fprintf(stderr, "usage: nmd-hybrid ROM --frames N --idle START:END [--sym FILE] [--input FILE] "
                        "[--trace FILE] [--state-out FILE]\n");
        return 2;
    }

    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 2; }
    fseek(f, 0, SEEK_END);
    size_t rom_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(rom_size);
    if (!rom || fread(rom, 1, rom_size, f) != rom_size) { perror(rom_path); return 2; }
    fclose(f);

    /* Pad script: per-port arrays for the null host. */
    uint16_t *pads[2] = { NULL, NULL };
    size_t npads = 0;
    if (input) {
        FILE *in = fopen(input, "r");
        if (!in) { perror(input); return 2; }
        char line[128];
        while (fgets(line, sizeof line, in)) {
            unsigned p1 = 0, p2 = 0;
            if (line[0] == '#' || sscanf(line, "%x %x", &p1, &p2) < 1) continue;
            for (int p = 0; p < 2; p++) pads[p] = realloc(pads[p], (npads + 1) * sizeof **pads);
            pads[0][npads] = (uint16_t)p1;
            pads[1][npads] = (uint16_t)p2;
            npads++;
        }
        fclose(in);
    }
    nmd_null_host host = { .script = { pads[0], pads[1] }, .script_len = { npads, npads } };
    nmd_config cfg = { .video = NMD_VIDEO_NTSC, .host = nmd_null_host_make(&host) };
    if (!nmd_init(&cfg, game, rom, rom_size)) return 2;
    nmd_set_fatal_handler(fatal_handler);

    oracle_setup();
    for (int i = 0; i < nsyms; i++)
        if (!oracle_load_syms(syms[i])) { perror(syms[i]); return 2; }
    char lo[64], hi[64] = "";
    uint32_t start, end;
    if (sscanf(idle, "%63[^:]:%63s", lo, hi) != 2 || !oracle_resolve(lo, &start, NULL, NULL) ||
        !oracle_resolve(hi, &end, NULL, NULL)) {
        fprintf(stderr, "nmd-hybrid: bad --idle '%s' (START:END, labels or hex)\n", idle);
        return 2;
    }
    oracle_set_idle(start, end);
    oracle_install_hybrid(max_cycles);

    FILE *trace = NULL;
    if (trace_path && !(trace = fopen(trace_path, "w"))) { perror(trace_path); return 2; }
    nmd_trace_open(trace);
    uint64_t done = nmd_run_frames(frames);
    nmd_trace_open(NULL);
    if (trace) fclose(trace);
    if (state_out && !nmd_state_save(state_out)) { perror(state_out); return 2; }
    printf("%s: %llu frames, %zu routine(s) in C\n", game->name, (unsigned long long)done, game->routine_count);
    nmd_shutdown();
    free(rom);
    free(pads[0]);
    free(pads[1]);
    return done == frames ? 0 : 1;
}
