/*
 * mkhello - writes the "hello" test ROM image used by tests/hello.
 *
 * The image is ours (no third-party code or data): vectors, a valid header,
 * a palette and a message. The routine addresses hold a placeholder
 * (STOP #$2700) because the C in tests/hello/hello_game.c stands in for the
 * 68000 program. A real 68000 version for the oracle comes with milestone 2.
 *
 * Usage: mkhello OUT.bin
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hello_layout.h"

static uint8_t rom[HELLO_ROM_SIZE];

static void put16(uint32_t at, uint16_t v) { rom[at] = (uint8_t)(v >> 8); rom[at + 1] = (uint8_t)v; }
static void put32(uint32_t at, uint32_t v) { put16(at, (uint16_t)(v >> 16)); put16(at + 2, (uint16_t)v); }

static void put_str(uint32_t at, size_t field, const char *s)
{
    memset(rom + at, ' ', field);
    memcpy(rom + at, s, strlen(s) < field ? strlen(s) : field);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.bin\n", argv[0]);
        return 2;
    }

    /* Vectors: everything not used goes to the error trap. */
    for (uint32_t v = 0; v < 0x100; v += 4) put32(v, HELLO_ADDR_ERROR);
    put32(0x000, HELLO_INITIAL_SSP);
    put32(0x004, HELLO_ADDR_MAIN);
    put32(0x070, HELLO_ADDR_HINT);
    put32(0x078, HELLO_ADDR_VINT);

    /* Header. */
    put_str(0x100, 16, "SEGA MEGA DRIVE");
    put_str(0x110, 16, "(C)NMD  2026.NOV");
    put_str(0x120, 48, "NATIVEMD HELLO");
    put_str(0x150, 48, "NATIVEMD HELLO");
    put_str(0x180, 14, "GM 00000000-00");
    put_str(0x190, 16, "J");
    put32(0x1A0, 0x000000);
    put32(0x1A4, HELLO_ROM_SIZE - 1);
    put32(0x1A8, 0xFF0000);
    put32(0x1AC, 0xFFFFFF);
    put_str(0x1B0, 12, "");
    put_str(0x1BC, 12, "");
    put_str(0x1C8, 40, "");
    put_str(0x1F0, 16, "JUE");

    /* Placeholder code: STOP #$2700 at each routine. */
    const uint32_t routines[] = { HELLO_ADDR_MAIN, HELLO_ADDR_HINT, HELLO_ADDR_VINT, HELLO_ADDR_ERROR };
    for (size_t i = 0; i < sizeof routines / sizeof routines[0]; i++) {
        put16(routines[i], 0x4E72);
        put16(routines[i] + 2, 0x2700);
    }

    /* Data. */
    static const uint16_t palette[16] = {
        0x0000, 0x0EEE, 0x000E, 0x00E0, 0x0E00, 0x00EE, 0x0E0E, 0x0EE0,
        0x0222, 0x0444, 0x0666, 0x0888, 0x0AAA, 0x0CCC, 0x0246, 0x0642,
    };
    for (int i = 0; i < 16; i++) put16(HELLO_ADDR_PALETTE + 2u * (unsigned)i, palette[i]);
    memcpy(rom + HELLO_ADDR_MESSAGE, HELLO_MESSAGE, sizeof HELLO_MESSAGE);

    /* Checksum over $200..end. */
    uint16_t sum = 0;
    for (uint32_t i = 0x200; i < HELLO_ROM_SIZE; i += 2)
        sum = (uint16_t)(sum + (rom[i] << 8 | rom[i + 1]));
    put16(0x18E, sum);

    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(rom, 1, sizeof rom, f) != sizeof rom || fclose(f) != 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
