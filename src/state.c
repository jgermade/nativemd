/*
 * state.c - machine state files (docs/harness.md, "State files").
 *
 * Format version 2, all multi-byte fields big-endian:
 *   "NMDS", u16 version (2), u16 reserved
 *   u32 d[8], u32 a[8], u16 sr, u16 pad
 *   u8 ram[65536], u8 vram[65536], u16 cram[64], u16 vsram[40], u8 vdp_reg[24],
 *   u8 z80_ram[8192]                                   (end of version 1)
 *   VDP latches:  u8 code, u8 cmd_pending, u8 fill_pending, u8 in_vblank,
 *                 u8 vint_pending, u8 pad[3], u16 addr, u16 read_buffer,
 *                 u16 hint_counter (two's complement), u16 pad
 *   I/O:          u8 data[3], u8 ctrl[3], u16 pad_buttons[2]
 *   Z80 control:  u8 busreq, u8 reset, u8 psg_last, u8 pad, u32 bank,
 *                 u8 ym_addr[2], u8 ym_reg[2][256]
 * Version 1 files load with the latches at their reset values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define STATE_VERSION 2

/* ---- encoding -------------------------------------------------------------- */

static void put8(FILE *f, unsigned v)   { fputc((int)(v & 0xFF), f); }
static void put16(FILE *f, unsigned v)  { put8(f, v >> 8); put8(f, v); }
static void put32(FILE *f, uint32_t v)  { put16(f, v >> 16); put16(f, v & 0xFFFF); }
static void putn(FILE *f, const void *p, size_t n) { fwrite(p, 1, n, f); }

typedef struct reader { FILE *f; bool ok; } reader;

static unsigned get8(reader *r)
{
    int c = fgetc(r->f);
    if (c == EOF) { r->ok = false; return 0; }
    return (unsigned)c;
}
static unsigned get16(reader *r) { unsigned hi = get8(r); return hi << 8 | get8(r); }
static uint32_t get32(reader *r) { uint32_t hi = get16(r); return hi << 16 | get16(r); }
static void getn(reader *r, void *p, size_t n) { if (fread(p, 1, n, r->f) != n) r->ok = false; }

bool nmd_state_write(const char *path, const nmd_state_image *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    putn(f, "NMDS", 4);
    put16(f, STATE_VERSION);
    put16(f, 0);
    for (int i = 0; i < 8; i++) put32(f, s->regs.d[i]);
    for (int i = 0; i < 8; i++) put32(f, s->regs.a[i]);
    put16(f, s->regs.sr);
    put16(f, 0);
    putn(f, s->ram, sizeof s->ram);
    putn(f, s->vram, sizeof s->vram);
    for (int i = 0; i < 64; i++) put16(f, s->cram[i]);
    for (int i = 0; i < 40; i++) put16(f, s->vsram[i]);
    putn(f, s->vdp_reg, sizeof s->vdp_reg);
    putn(f, s->z80_ram, sizeof s->z80_ram);

    put8(f, s->vdp_code); put8(f, s->vdp_cmd_pending); put8(f, s->vdp_fill_pending);
    put8(f, s->vdp_in_vblank); put8(f, s->vdp_vint_pending); put8(f, 0); put8(f, 0); put8(f, 0);
    put16(f, s->vdp_addr); put16(f, s->vdp_read_buffer); put16(f, (uint16_t)s->vdp_hint_counter); put16(f, 0);

    putn(f, s->io_data, 3); putn(f, s->io_ctrl, 3);
    put16(f, s->io_pad[0]); put16(f, s->io_pad[1]);

    put8(f, s->z80_busreq); put8(f, s->z80_reset); put8(f, s->psg_last); put8(f, 0);
    put32(f, s->z80_bank);
    putn(f, s->ym_addr, 2);
    putn(f, s->ym_reg, sizeof s->ym_reg);

    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

static void reset_latches(nmd_state_image *s)
{
    s->vdp_code = 0; s->vdp_cmd_pending = s->vdp_fill_pending = false;
    s->vdp_in_vblank = s->vdp_vint_pending = false;
    s->vdp_addr = s->vdp_read_buffer = 0; s->vdp_hint_counter = 0;
    memset(s->io_data, 0x7F, 3); memset(s->io_ctrl, 0, 3); s->io_pad[0] = s->io_pad[1] = 0;
    s->z80_busreq = false; s->z80_reset = true; s->psg_last = 0; s->z80_bank = 0;
    memset(s->ym_addr, 0, 2); memset(s->ym_reg, 0, sizeof s->ym_reg);
}

bool nmd_state_read(const char *path, nmd_state_image *s)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    reader r = { f, true };
    char magic[4];
    getn(&r, magic, 4);
    unsigned version = get16(&r);
    (void)get16(&r);
    if (!r.ok || memcmp(magic, "NMDS", 4) != 0 || version < 1 || version > STATE_VERSION) {
        fclose(f);
        return false;
    }
    for (int i = 0; i < 8; i++) s->regs.d[i] = get32(&r);
    for (int i = 0; i < 8; i++) s->regs.a[i] = get32(&r);
    s->regs.sr = (uint16_t)get16(&r);
    (void)get16(&r);
    getn(&r, s->ram, sizeof s->ram);
    getn(&r, s->vram, sizeof s->vram);
    for (int i = 0; i < 64; i++) s->cram[i] = (uint16_t)get16(&r);
    for (int i = 0; i < 40; i++) s->vsram[i] = (uint16_t)get16(&r);
    getn(&r, s->vdp_reg, sizeof s->vdp_reg);
    getn(&r, s->z80_ram, sizeof s->z80_ram);

    reset_latches(s);
    if (version >= 2) {
        s->vdp_code = (uint8_t)get8(&r); s->vdp_cmd_pending = get8(&r); s->vdp_fill_pending = get8(&r);
        s->vdp_in_vblank = get8(&r); s->vdp_vint_pending = get8(&r); get8(&r); get8(&r); get8(&r);
        s->vdp_addr = (uint16_t)get16(&r); s->vdp_read_buffer = (uint16_t)get16(&r);
        s->vdp_hint_counter = (int16_t)(uint16_t)get16(&r); (void)get16(&r);
        getn(&r, s->io_data, 3); getn(&r, s->io_ctrl, 3);
        s->io_pad[0] = (uint16_t)get16(&r); s->io_pad[1] = (uint16_t)get16(&r);
        s->z80_busreq = get8(&r); s->z80_reset = get8(&r); s->psg_last = (uint8_t)get8(&r); get8(&r);
        s->z80_bank = get32(&r);
        getn(&r, s->ym_addr, 2);
        getn(&r, s->ym_reg, sizeof s->ym_reg);
    }
    fclose(f);
    return r.ok;
}

/* ---- machine <-> image ------------------------------------------------------ */

void nmd_state_capture(nmd_state_image *s)
{
    s->regs = M68K;
    memcpy(s->ram, NMD.ram, sizeof s->ram);
    memcpy(s->vram, NMD.vdp.vram, sizeof s->vram);
    memcpy(s->cram, NMD.vdp.cram, sizeof s->cram);
    memcpy(s->vsram, NMD.vdp.vsram, sizeof s->vsram);
    memcpy(s->vdp_reg, NMD.vdp.reg, sizeof s->vdp_reg);
    memcpy(s->z80_ram, NMD.z80.ram, sizeof s->z80_ram);
    s->vdp_code = NMD.vdp.code;
    s->vdp_cmd_pending = NMD.vdp.cmd_pending;
    s->vdp_fill_pending = NMD.vdp.fill_pending;
    s->vdp_in_vblank = NMD.vdp.in_vblank;
    s->vdp_vint_pending = NMD.vdp.vint_pending;
    s->vdp_addr = NMD.vdp.addr;
    s->vdp_read_buffer = NMD.vdp.read_buffer;
    s->vdp_hint_counter = (int16_t)NMD.vdp.hint_counter;
    memcpy(s->io_data, NMD.io.data, 3);
    memcpy(s->io_ctrl, NMD.io.ctrl, 3);
    s->io_pad[0] = NMD.io.pad[0];
    s->io_pad[1] = NMD.io.pad[1];
    s->z80_busreq = NMD.z80.busreq;
    s->z80_reset = NMD.z80.reset;
    s->psg_last = NMD.z80.psg_last;
    s->z80_bank = NMD.z80.bank;
    memcpy(s->ym_addr, NMD.z80.ym_addr, 2);
    memcpy(s->ym_reg, NMD.z80.ym_reg, sizeof s->ym_reg);
}

void nmd_state_apply(const nmd_state_image *s)
{
    M68K = s->regs;
    memcpy(NMD.ram, s->ram, sizeof s->ram);
    memcpy(NMD.vdp.vram, s->vram, sizeof s->vram);
    memcpy(NMD.vdp.cram, s->cram, sizeof s->cram);
    memcpy(NMD.vdp.vsram, s->vsram, sizeof s->vsram);
    memcpy(NMD.vdp.reg, s->vdp_reg, sizeof s->vdp_reg);
    memcpy(NMD.z80.ram, s->z80_ram, sizeof s->z80_ram);
    NMD.vdp.code = s->vdp_code;
    NMD.vdp.cmd_pending = s->vdp_cmd_pending;
    NMD.vdp.fill_pending = s->vdp_fill_pending;
    NMD.vdp.in_vblank = s->vdp_in_vblank;
    NMD.vdp.vint_pending = s->vdp_vint_pending;
    NMD.vdp.addr = s->vdp_addr;
    NMD.vdp.read_buffer = s->vdp_read_buffer;
    NMD.vdp.hint_counter = s->vdp_hint_counter;
    memcpy(NMD.io.data, s->io_data, 3);
    memcpy(NMD.io.ctrl, s->io_ctrl, 3);
    NMD.io.pad[0] = s->io_pad[0];
    NMD.io.pad[1] = s->io_pad[1];
    NMD.z80.busreq = s->z80_busreq;
    NMD.z80.reset = s->z80_reset;
    NMD.z80.psg_last = s->psg_last;
    NMD.z80.bank = s->z80_bank;
    memcpy(NMD.z80.ym_addr, s->ym_addr, 2);
    memcpy(NMD.z80.ym_reg, s->ym_reg, sizeof s->ym_reg);
}

bool nmd_state_save(const char *path)
{
    nmd_state_image *s = malloc(sizeof *s);
    if (!s) return false;
    nmd_state_capture(s);
    bool ok = nmd_state_write(path, s);
    free(s);
    return ok;
}

bool nmd_state_load(const char *path)
{
    nmd_state_image *s = malloc(sizeof *s);
    if (!s) return false;
    bool ok = nmd_state_read(path, s);
    if (ok) nmd_state_apply(s);
    free(s);
    return ok;
}
