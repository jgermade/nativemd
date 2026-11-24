/*
 * state.c - machine state files (docs/harness.md, "State files").
 *
 * Layout, all multi-byte fields big-endian:
 *   "NMDS"            magic
 *   u16 version       1
 *   u16 reserved      0
 *   u32 d[8], a[8]    68000 data/address registers
 *   u16 sr, u16 pad
 *   u8  ram[65536]
 *   u8  vram[65536]
 *   u16 cram[64]
 *   u16 vsram[40]
 *   u8  vdp_reg[24]
 *   u8  z80_ram[8192]
 */
#include <stdio.h>
#include <string.h>

#include "internal.h"

#define STATE_VERSION 1

static void put16(FILE *f, uint16_t v) { fputc(v >> 8, f); fputc(v & 0xFF, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (uint16_t)(v >> 16)); put16(f, (uint16_t)v); }

static bool get16(FILE *f, uint16_t *v)
{
    int a = fgetc(f), b = fgetc(f);
    if (a == EOF || b == EOF) return false;
    *v = (uint16_t)(a << 8 | b);
    return true;
}

static bool get32(FILE *f, uint32_t *v)
{
    uint16_t hi, lo;
    if (!get16(f, &hi) || !get16(f, &lo)) return false;
    *v = (uint32_t)hi << 16 | lo;
    return true;
}

bool nmd_state_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("NMDS", 1, 4, f);
    put16(f, STATE_VERSION);
    put16(f, 0);
    for (int i = 0; i < 8; i++) put32(f, M68K.d[i]);
    for (int i = 0; i < 8; i++) put32(f, M68K.a[i]);
    put16(f, M68K.sr);
    put16(f, 0);
    fwrite(NMD.ram, 1, sizeof NMD.ram, f);
    fwrite(NMD.vdp.vram, 1, sizeof NMD.vdp.vram, f);
    for (int i = 0; i < 64; i++) put16(f, NMD.vdp.cram[i]);
    for (int i = 0; i < 40; i++) put16(f, NMD.vdp.vsram[i]);
    fwrite(NMD.vdp.reg, 1, sizeof NMD.vdp.reg, f);
    fwrite(NMD.z80.ram, 1, sizeof NMD.z80.ram, f);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

bool nmd_state_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    uint16_t version, pad;
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "NMDS", 4) == 0 &&
              get16(f, &version) && version == STATE_VERSION && get16(f, &pad);
    m68k_regs r;
    for (int i = 0; ok && i < 8; i++) ok = get32(f, &r.d[i]);
    for (int i = 0; ok && i < 8; i++) ok = get32(f, &r.a[i]);
    ok = ok && get16(f, &r.sr) && get16(f, &pad);
    ok = ok && fread(NMD.ram, 1, sizeof NMD.ram, f) == sizeof NMD.ram;
    ok = ok && fread(NMD.vdp.vram, 1, sizeof NMD.vdp.vram, f) == sizeof NMD.vdp.vram;
    for (int i = 0; ok && i < 64; i++) ok = get16(f, &NMD.vdp.cram[i]);
    for (int i = 0; ok && i < 40; i++) ok = get16(f, &NMD.vdp.vsram[i]);
    ok = ok && fread(NMD.vdp.reg, 1, sizeof NMD.vdp.reg, f) == sizeof NMD.vdp.reg;
    ok = ok && fread(NMD.z80.ram, 1, sizeof NMD.z80.ram, f) == sizeof NMD.z80.ram;
    fclose(f);
    if (ok) M68K = r;
    return ok;
}
