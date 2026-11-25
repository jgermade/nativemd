/*
 * refemu - run a ROM headless on a libretro Mega Drive core (a reference emulator) and
 * dump the 68000 RAM, for comparing hardware behaviour with nativemd (docs/harness.md,
 * "Reference emulators").
 *
 * Usage: refemu CORE.so ROM FRAMES RAM_OUT [MAGIC]
 *   Runs FRAMES frames with no input and writes the 64 KB of 68000 RAM to RAM_OUT in
 *   big-endian order (as the 68000 sees it). Cores keep RAM in host word order; MAGIC
 *   (4 ASCII bytes, default NMDT) at $FF0000 tells which: it must appear either as is or
 *   byte-swapped, otherwise the run fails.
 *
 * Only the libretro API is used: no emulator code is part of nativemd.
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

static char dir[] = "/tmp";

static bool env(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = dir;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (cmd == RETRO_ENVIRONMENT_GET_CAN_DUPE) *(bool *)data = true;
        return true;
    default:
        return false;
    }
}

static void video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t n) { (void)d; return n; }
static void poll(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id) { (void)port; (void)dev; (void)idx; (void)id; return 0; }

#define SYM(name) do { *(void **)&name = dlsym(h, #name); if (!name) { fprintf(stderr, "refemu: %s missing\n", #name); return 2; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: refemu CORE.so ROM FRAMES RAM_OUT [MAGIC]\n"); return 2; }
    const char *magic = argc > 5 ? argv[5] : "NMDT";
    if (strlen(magic) != 4) { fprintf(stderr, "refemu: MAGIC must be 4 bytes\n"); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "refemu: %s\n", dlerror()); return 2; }
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_run)(void);
    void *(*retro_get_memory_data)(unsigned);
    size_t (*retro_get_memory_size)(unsigned);
    void (*retro_unload_game)(void);
    void (*retro_deinit)(void);
    SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
    SYM(retro_init); SYM(retro_load_game); SYM(retro_run); SYM(retro_get_memory_data);
    SYM(retro_get_memory_size); SYM(retro_unload_game); SYM(retro_deinit);

    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *rom = malloc((size_t)n);
    if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n) { perror(argv[2]); return 2; }
    fclose(f);

    retro_set_environment(env);
    retro_init();
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(poll);
    retro_set_input_state(input);
    struct retro_game_info info = { .path = argv[2], .data = rom, .size = (size_t)n, .meta = NULL };
    if (!retro_load_game(&info)) { fprintf(stderr, "refemu: the core did not load %s\n", argv[2]); return 2; }
    long frames = strtol(argv[3], NULL, 10);
    for (long i = 0; i < frames; i++) retro_run();

    unsigned char *ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || size < 0x10000) { fprintf(stderr, "refemu: no 64 KB system RAM (%zu)\n", size); return 2; }
    unsigned char out[0x10000];
    memcpy(out, ram, sizeof out);
    char swapped[4] = { magic[1], magic[0], magic[3], magic[2] };
    if (!memcmp(out, swapped, 4)) {
        for (size_t i = 0; i < sizeof out; i += 2) { unsigned char t = out[i]; out[i] = out[i + 1]; out[i + 1] = t; }
    } else if (memcmp(out, magic, 4)) {
        fprintf(stderr, "refemu: no %s at $FF0000 after %ld frames (test did not finish?)\n", magic, frames);
        return 1;
    }
    f = fopen(argv[4], "wb");
    if (!f || fwrite(out, 1, sizeof out, f) != sizeof out) { perror(argv[4]); return 2; }
    fclose(f);
    retro_unload_game();
    retro_deinit();
    dlclose(h);
    free(rom);
    return 0;
}
