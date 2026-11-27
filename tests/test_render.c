/*
 * test_render.c - the VDP renderer (src/render.c) on hand-made VRAM, CRAM, VSRAM and
 * registers: the backdrop, plane B and A tiles with flips and palettes, priorities
 * between planes and sprites, horizontal scroll per line, the window, the sprite link
 * list and its per-line limit, the display-off backdrop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"

static int failures;
#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void idle(void) { for (;;) wait_vblank(); }
static const nmd_routine routines[] = { { 0x000200, idle, "Idle" } };
static const nmd_game game = { .name = "render", .routines = routines, .routine_count = 1 };
static uint8_t rom[0x1000];

#define RED   0xFFFF0000u
#define GREEN 0xFF00FF00u
#define BLUE  0xFF0000FFu
#define WHITE 0xFFFFFFFFu

static uint8_t *vram, *reg;
static uint16_t *cram, *vsram;

static void setup(void)
{
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0x100, "SEGA MEGA DRIVE ", 16);
    rom[0x004 + 2] = 0x02;                              /* reset PC $200 */
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    if (!nmd_init(&cfg, &game, rom, sizeof rom)) { fprintf(stderr, "init failed\n"); exit(2); }
    vram = nmd_vram(); reg = nmd_vdp_regs(); cram = nmd_cram(); vsram = nmd_vsram();
    reg[1] = 0x44;                                      /* display on, mode 5, 224 lines */
    reg[2] = 0x30;                                      /* plane A at $C000 */
    reg[4] = 0x07;                                      /* plane B at $E000 */
    reg[5] = 0x7C;                                      /* sprites at $F800 */
    reg[7] = 0x05;                                      /* backdrop: colour 5 */
    reg[12] = 0x81;                                     /* H40 */
    reg[13] = 0x3F;                                     /* H scroll at $FC00 */
    reg[16] = 0x01;                                     /* 64x32 planes */
    cram[5] = 0x0E00;                                   /* blue */
    cram[3] = 0x000E;                                   /* red */
    cram[4] = 0x00E0;                                   /* green */
    cram[16 + 2] = 0x0EEE;                              /* palette line 1, colour 2: white */
    /* Tile 1: top-left pixel colour 3; tile 2: all colour 4; tile 3: all colour 2. */
    vram[32 * 1] = 0x30;
    memset(vram + 32 * 2, 0x44, 32);
    memset(vram + 32 * 3, 0x22, 32);
    vram[0xF800 + 0] = 0; vram[0xF800 + 1] = 0;         /* sprite 0 off screen, end of list */
}

static void put16(unsigned a, uint16_t v) { vram[a] = (uint8_t)(v >> 8); vram[a + 1] = (uint8_t)v; }
static void sprite(unsigned i, unsigned y, unsigned size, unsigned link, uint16_t name, unsigned x)
{
    unsigned a = 0xF800 + i * 8;
    put16(a, (uint16_t)y); vram[a + 2] = (uint8_t)size; vram[a + 3] = (uint8_t)link;
    put16(a + 4, name); put16(a + 6, (uint16_t)x);
}
static uint32_t pixel(int x, int y)
{
    int w, h;
    const uint32_t *fb = nmd_framebuffer(&w, &h);
    return fb[y * 320 + x];
}

int main(void)
{
    setup();
    nmd_run_frames(1);
    int w, h;
    nmd_framebuffer(&w, &h);
    CHECK(w == 320 && h == 224, "H40, 224 lines: %dx%d", w, h);
    CHECK(pixel(0, 0) == BLUE && pixel(319, 223) == BLUE, "backdrop %08X", pixel(0, 0));

    put16(0xE000, 0x0001);                              /* plane B (0,0): tile 1 */
    put16(0xE002, 0x0801);                              /* (1,0): tile 1, H flip */
    put16(0xE000 + 128, 0x1001);                        /* (0,1): tile 1, V flip */
    nmd_run_frames(1);
    CHECK(pixel(0, 0) == RED && pixel(1, 0) == BLUE, "plane B tile pixel");
    CHECK(pixel(15, 0) == RED && pixel(8, 0) == BLUE, "H flip");
    CHECK(pixel(0, 15) == RED && pixel(0, 8) == BLUE, "V flip");

    put16(0xC000 + 4, 0x2003);                          /* plane A (2,0): tile 3, palette 1 */
    put16(0xE000 + 4, 0x8002);                          /* plane B (2,0): tile 2, high */
    nmd_run_frames(1);
    CHECK(pixel(16, 0) == GREEN, "high plane B over low plane A: %08X", pixel(16, 0));
    put16(0xC000 + 4, 0xA003);                          /* plane A high too */
    nmd_run_frames(1);
    CHECK(pixel(16, 0) == WHITE, "plane A over plane B at the same priority: %08X", pixel(16, 0));

    sprite(0, 128 + 0, 0, 0, 0x0002, 128 + 16);         /* low sprite on the high plane A */
    nmd_run_frames(1);
    CHECK(pixel(16, 0) == WHITE, "high plane A over a low sprite");
    sprite(0, 128 + 0, 0, 0, 0x8002, 128 + 16);
    nmd_run_frames(1);
    CHECK(pixel(16, 0) == GREEN && pixel(24, 0) == BLUE, "high sprite on top, 8 px wide");

    /* Sprite list: 21 one-cell sprites on line 40, 8 px apart: the 21st is not drawn. */
    for (unsigned i = 1; i <= 21; i++) sprite(i, 128 + 40, 0, i < 21 ? i + 1 : 0, 0x0002, 128 + 8 * i);
    sprite(0, 0, 0, 1, 0x0002, 0);                      /* sprite 0 off screen, links to 1 */
    nmd_run_frames(1);
    CHECK(pixel(8, 40) == GREEN && pixel(8 * 20, 40) == GREEN, "20 sprites on a line");
    CHECK(pixel(8 * 21, 40) == BLUE, "the 21st sprite on a line is dropped: %08X", pixel(8 * 21, 40));

    /* Horizontal scroll per line: plane B line 0 moved 3 px right. */
    reg[11] = 0x03;
    put16(0xFC00 + 0 * 4 + 2, 3);
    nmd_run_frames(1);
    CHECK(pixel(3, 0) == RED && pixel(0, 0) == BLUE && pixel(0, 1) == BLUE, "line scroll");
    reg[11] = 0;

    /* Window: the top 8 lines over plane A, from the window name table at $B000. */
    reg[3] = 0x2C; reg[18] = 0x01;                      /* window at $B000, lines 0-7 */
    put16(0xB000, 0x8002);
    nmd_run_frames(1);
    CHECK(pixel(0, 0) == GREEN && pixel(16, 0) != WHITE, "window replaces plane A: %08X", pixel(16, 0));
    reg[18] = 0;

    reg[1] = 0x04;                                      /* display off */
    nmd_run_frames(1);
    CHECK(pixel(16, 0) == BLUE && pixel(100, 100) == BLUE, "display off: backdrop");

    nmd_shutdown();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("test_render: ok\n");
    return 0;
}
