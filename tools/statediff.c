/*
 * nmd-statediff - compare two .nmds state files (docs/harness.md).
 *
 * Usage: nmd-statediff [--regs LIST] [--ignore-ram LO-HI]... EXPECTED ACTUAL
 *
 *   --regs        comma-separated registers to compare (d0..d7,a0..a7,sr);
 *                 none by default, since C code need not keep values in them
 *   --ignore-ram  hex 24-bit range of 68000 RAM to skip (e.g. the stack)
 *
 * Exit status: 0 equal, 1 different, 2 error.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_SIZE 139572u

enum { OFF_D = 8, OFF_A = 40, OFF_SR = 72, OFF_RAM = 76, OFF_VRAM = 65612, OFF_CRAM = 131148,
       OFF_VSRAM = 131276, OFF_VREG = 131356, OFF_Z80 = 131380 };

static uint8_t *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    uint8_t *b = malloc(STATE_SIZE + 1);
    size_t n = fread(b, 1, STATE_SIZE + 1, f);
    fclose(f);
    if (n != STATE_SIZE || memcmp(b, "NMDS", 4) != 0 || b[4] != 0 || b[5] != 1) {
        fprintf(stderr, "%s: not a version-1 .nmds file\n", path);
        exit(2);
    }
    return b;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int differences;

static void region(const char *name, const uint8_t *a, const uint8_t *b, uint32_t off, uint32_t size,
                   uint32_t base, const uint32_t (*ignore)[2], int nignore)
{
    int shown = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t addr = base + i;
        bool skip = false;
        for (int k = 0; k < nignore; k++)
            if (addr >= ignore[k][0] && addr <= ignore[k][1]) skip = true;
        if (skip || a[off + i] == b[off + i]) continue;
        differences++;
        if (shown++ < 8)
            printf("  %-6s %06X: expected %02X, actual %02X\n", name, addr, a[off + i], b[off + i]);
    }
    if (shown > 8) printf("  %-6s ... %d differing bytes\n", name, shown);
}

int main(int argc, char **argv)
{
    uint32_t ignore[16][2];
    int nignore = 0;
    char regs[128] = "";
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "--ignore-ram") && argi + 1 < argc && nignore < 16 &&
            sscanf(argv[argi + 1], "%x-%x", &ignore[nignore][0], &ignore[nignore][1]) == 2) {
            nignore++;
            argi += 2;
        } else if (!strcmp(argv[argi], "--regs") && argi + 1 < argc) {
            snprintf(regs, sizeof regs, "%s", argv[argi + 1]);
            argi += 2;
        } else {
            argi = argc;
            break;
        }
    }
    if (argc - argi != 2) {
        fprintf(stderr, "usage: %s [--regs LIST] [--ignore-ram LO-HI]... EXPECTED ACTUAL\n", argv[0]);
        return 2;
    }
    uint8_t *a = load(argv[argi]), *b = load(argv[argi + 1]);

    for (char *tok = strtok(regs, ","); tok; tok = strtok(NULL, ",")) {
        uint32_t off;
        if ((tok[0] == 'd' || tok[0] == 'a') && tok[1] >= '0' && tok[1] <= '7' && !tok[2])
            off = (tok[0] == 'd' ? OFF_D : OFF_A) + 4u * (unsigned)(tok[1] - '0');
        else if (!strcmp(tok, "sr")) off = OFF_SR;
        else { fprintf(stderr, "unknown register %s\n", tok); return 2; }
        uint32_t va = off == OFF_SR ? (uint32_t)(a[off] << 8 | a[off + 1]) : be32(a + off);
        uint32_t vb = off == OFF_SR ? (uint32_t)(b[off] << 8 | b[off + 1]) : be32(b + off);
        if (va != vb) { differences++; printf("  %-6s expected %08X, actual %08X\n", tok, va, vb); }
    }
    region("RAM",   a, b, OFF_RAM,   65536, 0xFF0000, (const uint32_t (*)[2])ignore, nignore);
    region("VRAM",  a, b, OFF_VRAM,  65536, 0, NULL, 0);
    region("CRAM",  a, b, OFF_CRAM,  128, 0, NULL, 0);
    region("VSRAM", a, b, OFF_VSRAM, 80, 0, NULL, 0);
    region("VDPREG", a, b, OFF_VREG, 24, 0, NULL, 0);
    region("Z80RAM", a, b, OFF_Z80,  8192, 0, NULL, 0);

    if (differences) { printf("states differ (%d)\n", differences); return 1; }
    printf("states match\n");
    return 0;
}
