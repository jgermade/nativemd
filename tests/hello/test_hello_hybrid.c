/*
 * test_hello_hybrid - hybrid execution: only some routines have C; the rest
 * run as the original 68000 code on the oracle CPU, inside the engine's
 * frame loop. The trace must be the same as the full-oracle and full-C runs.
 *
 *   hint:    HInt in C (C -> ASM: Main and VInt are original code)
 *   readpad: HInt and ReadPad in C (also ASM -> C: VInt calls ReadPad by BSR)
 *
 * Usage: test_hello_hybrid hint|readpad HELLO.bin HELLO.sym TRACE_OUT STATE_OUT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"
#include "oracle/oracle.h"
#include "hello_layout.h"

extern const nmd_game hello_game;

int main(int argc, char **argv)
{
    if (argc != 6) { fprintf(stderr, "usage: %s hint|readpad HELLO.bin HELLO.sym TRACE_OUT STATE_OUT\n", argv[0]); return 2; }
    bool with_readpad = strcmp(argv[1], "readpad") == 0;
    argv++;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t rom[HELLO_ROM_SIZE];
    size_t rom_size = fread(rom, 1, sizeof rom, f);
    fclose(f);

    /* Keep only some routines from the C routine table. */
    static nmd_routine partial[2];
    size_t n = 0;
    for (size_t i = 0; i < hello_game.routine_count; i++) {
        uint32_t a = hello_game.routines[i].addr;
        if (a == HELLO_ADDR_HINT || (with_readpad && a == HELLO_ADDR_READPAD))
            partial[n++] = hello_game.routines[i];
    }
    nmd_game game = hello_game;
    game.routines = partial;
    game.routine_count = n;

    static uint16_t script[10];
    for (int i = 3; i < 10; i++) script[i] = NMD_PAD_START;
    nmd_null_host host = { .script = { script, NULL }, .script_len = { 10, 0 } };
    nmd_config cfg = { .video = NMD_VIDEO_NTSC, .host = nmd_null_host_make(&host) };
    if (!nmd_init(&cfg, &game, rom, rom_size)) return 2;

    oracle_setup();
    if (!oracle_load_syms(argv[2])) { perror(argv[2]); return 2; }
    uint32_t lo, hi;
    bool has_next;
    if (!oracle_resolve("WaitLoop", &lo, &hi, &has_next) || !has_next) { fprintf(stderr, "no WaitLoop\n"); return 2; }
    oracle_set_idle(lo, hi);
    oracle_install_hybrid(0);

    FILE *trace = fopen(argv[3], "w");
    if (!trace) { perror(argv[3]); return 2; }
    nmd_trace_open(trace);
    uint64_t frames = nmd_run_frames(10);
    nmd_trace_open(NULL);
    fclose(trace);
    if (!nmd_state_save(argv[4])) { perror(argv[4]); return 2; }

    const uint8_t *ram = nmd_ram();
    unsigned counter = (unsigned)(ram[0] << 8 | ram[1]);
    nmd_shutdown();
    if (frames != 10 || counter != 10) {
        fprintf(stderr, "FAIL: frames %llu, VInt counter %u\n", (unsigned long long)frames, counter);
        return 1;
    }
    printf("test_hello_hybrid: ok\n");
    return 0;
}
