/* play.c - play a game interactively (play.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "play.h"
#include "oracle/oracle.h"
#include "sdl/sdl_host.h"

static void fatal_handler(const char *msg)
{
    fprintf(stderr, "nativemd: fatal: %s (frame %llu)\n", msg, (unsigned long long)nmd_frame_count());
    exit(3);
}

int nmd_play_main(int argc, char **argv, const nmd_game *game, const char *default_idle)
{
    const char *rom_path = NULL, *idle = default_idle, *syms[4];
    int nsyms = 0, scale = 3;
    bool fullscreen = false;
    nmd_video video = NMD_VIDEO_NTSC;
    uint64_t frames = 0, max_cycles = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--idle") && v)       { idle = v; i++; }
        else if (!strcmp(a, "--sym") && v)        {
            if (nsyms == 4) { fprintf(stderr, "more than 4 --sym\n"); return 2; }
            syms[nsyms++] = v; i++;
        }
        else if (!strcmp(a, "--scale") && v)      { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen"))      fullscreen = true;
        else if (!strcmp(a, "--pal"))             video = NMD_VIDEO_PAL;
        else if (!strcmp(a, "--frames") && v)     { frames = strtoull(v, NULL, 10); i++; }
        else if (!strcmp(a, "--max-cycles") && v) { max_cycles = strtoull(v, NULL, 10); i++; }
        else if (a[0] != '-' && !rom_path)        rom_path = a;
        else { rom_path = NULL; break; }
    }
    if (!rom_path || !idle) {
        fprintf(stderr, "usage: %s ROM [--idle START:END] [--sym FILE] [--scale N] [--fullscreen] [--pal] "
                        "[--frames N] [--max-cycles N]\n"
                        "keys: arrows, Z/X/C = A/B/C, Enter = Start, P = pause, Tab = fast forward, "
                        "F11 = fullscreen, Esc = quit\n", argv[0]);
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

    char title[96];
    snprintf(title, sizeof title, "%s - nativemd", game->name);
    nmd_sdl *sdl = nmd_sdl_open(title, scale, fullscreen, video);
    if (!sdl) return 2;
    nmd_config cfg = { .video = video, .host = nmd_sdl_host(sdl) };
    if (!nmd_init(&cfg, game, rom, rom_size)) return 2;
    nmd_set_fatal_handler(fatal_handler);

    oracle_setup();
    for (int i = 0; i < nsyms; i++)
        if (!oracle_load_syms(syms[i])) { perror(syms[i]); return 2; }
    char lo[64], hi[64] = "";
    uint32_t start, end;
    if (sscanf(idle, "%63[^:]:%63s", lo, hi) != 2 || !oracle_resolve(lo, &start, NULL, NULL) ||
        !oracle_resolve(hi, &end, NULL, NULL)) {
        fprintf(stderr, "%s: bad --idle '%s' (START:END, labels or hex)\n", argv[0], idle);
        return 2;
    }
    oracle_set_idle(start, end);
    oracle_install_hybrid(max_cycles);
    fprintf(stderr, "%s: %zu routine(s) in C, the rest as original code\n", game->name, game->routine_count);

    while (!nmd_sdl_quit_requested(sdl) && !nmd_game_finished() && (!frames || nmd_frame_count() < frames))
        nmd_run_frames(1);
    nmd_shutdown();
    nmd_sdl_close(sdl);
    free(rom);
    return 0;
}
