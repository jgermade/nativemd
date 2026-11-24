/*
 * test_hello - runs the hello ROM end to end on the headless host.
 *
 * Usage: test_hello HELLO.bin [TRACE_OUT]
 * With TRACE_OUT, the bus trace of the whole run is written there (CTest
 * compares it against tests/hello/hello.trace with nmd-tracediff).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"
#include "hello_layout.h"

extern const nmd_game hello_game;

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static uint16_t ram16(uint32_t a) { const uint8_t *r = nmd_ram(); return (uint16_t)(r[a & 0xFFFF] << 8 | r[(a + 1) & 0xFFFF]); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s HELLO.bin [TRACE_OUT]\n", argv[0]); return 2; }
    size_t rom_size;
    uint8_t *rom = read_file(argv[1], &rom_size);
    if (!rom) return 2;

    FILE *trace = NULL;
    if (argc >= 3) {
        trace = fopen(argv[2], "w");
        if (!trace) { perror(argv[2]); return 2; }
    }

    /* START held from frame 3 on, port 1. */
    static uint16_t script[10];
    for (int i = 3; i < 10; i++) script[i] = NMD_PAD_START;
    nmd_null_host host_state = { .script = { script, NULL }, .script_len = { 10, 0 } };

    nmd_config cfg = { .video = NMD_VIDEO_NTSC, .overseas = false,
                       .host = nmd_null_host_make(&host_state) };
    if (!nmd_init(&cfg, &hello_game, rom, rom_size)) return 2;

    const nmd_cart_info *ci = nmd_cart();
    CHECK(strcmp(ci->system, "SEGA MEGA DRIVE") == 0, "system '%s'", ci->system);
    CHECK(strcmp(ci->name_overseas, "NATIVEMD HELLO") == 0, "name '%s'", ci->name_overseas);
    CHECK(ci->checksum_header == ci->checksum_actual, "checksum %04X vs %04X",
          ci->checksum_header, ci->checksum_actual);
    CHECK(ci->regions == (NMD_REGION_JAPAN | NMD_REGION_USA | NMD_REGION_EUROPE), "regions %X", ci->regions);

    nmd_trace_open(trace);
    uint64_t frames = nmd_run_frames(10);
    nmd_trace_open(NULL);
    if (trace) fclose(trace);

    CHECK(frames == 10, "frames %llu", (unsigned long long)frames);
    CHECK(host_state.frames_presented == 10, "presented %llu", (unsigned long long)host_state.frames_presented);
    CHECK(!nmd_game_finished(), "main returned");

    /* VINT ran once per frame. */
    CHECK(ram16(HELLO_RAM_FRAMES) == 10, "frame counter %u", ram16(HELLO_RAM_FRAMES));
    /* HINT every 28 lines of 224: 8 per frame. */
    CHECK(ram16(HELLO_RAM_HINTS) == 80, "hint counter %u", ram16(HELLO_RAM_HINTS));
    /* Pad was sampled with START held (bit 7). */
    CHECK(nmd_ram()[HELLO_RAM_PAD & 0xFFFF] == 0x80, "pad %02X", nmd_ram()[HELLO_RAM_PAD & 0xFFFF]);

    /* Palette DMA'd from ROM to CRAM. */
    const uint8_t *r = nmd_rom(NULL);
    for (int i = 0; i < 16; i++) {
        uint16_t want = (uint16_t)(r[HELLO_ADDR_PALETTE + 2 * i] << 8 | r[HELLO_ADDR_PALETTE + 2 * i + 1]);
        CHECK(nmd_cram()[i] == want, "cram[%d] %04X want %04X", i, nmd_cram()[i], want);
    }

    /* Message tiles in plane A. */
    const char *msg = HELLO_MESSAGE;
    const uint8_t *vram = nmd_vram();
    for (size_t i = 0; msg[i]; i++) {
        unsigned a = HELLO_TEXT_VRAM + 2 * (unsigned)i;
        uint16_t got = (uint16_t)(vram[a] << 8 | vram[a + 1]);
        CHECK(got == (uint16_t)(msg[i] - 0x20), "tile %zu: %04X", i, got);
    }

    /* Z80 program written while holding the bus. */
    CHECK(nmd_z80_ram()[0] == 0xF3 && nmd_z80_ram()[1] == 0x76, "z80 ram %02X %02X",
          nmd_z80_ram()[0], nmd_z80_ram()[1]);

    /* VDP registers as left by main. */
    CHECK(nmd_vdp_regs()[1] == 0x74, "reg1 %02X", nmd_vdp_regs()[1]);
    CHECK(nmd_vdp_regs()[15] == 2, "reg15 %02X", nmd_vdp_regs()[15]);

    nmd_shutdown();
    free(rom);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("test_hello: ok\n");
    return 0;
}
