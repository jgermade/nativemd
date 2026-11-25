/*
 * vdp.c - VDP ports, registers, memories and DMA.
 *
 * Behaviour-exact, not cycle-exact: DMA completes instantly, the FIFO is
 * always reported empty, and rendering is not implemented yet (milestone 4).
 * The frame loop already walks the active lines so HINT handlers run at the
 * right line counts.
 */
#include <string.h>

#include "internal.h"

#define V (NMD.vdp)

void vdp_reset(void)
{
    memset(&V, 0, sizeof V);
}

bool vdp_vint_enabled(void) { return (V.reg[1] & 0x20) != 0; }
void vdp_vint_ack(void)      { V.vint_pending = false; }

static unsigned autoinc(void) { return V.reg[15]; }

static unsigned active_lines(void) { return (V.reg[1] & 0x08) ? 240 : 224; }

/* ------------------------------------------------------------------------ */
/* Memory access through the data port                                      */
/* ------------------------------------------------------------------------ */

static void vram_wr16(uint16_t a, uint16_t v)
{
    /* Odd addresses swap bytes, as on hardware. */
    V.vram[a & 0xFFFE]       = (uint8_t)((a & 1) ? v : v >> 8);
    V.vram[(a & 0xFFFE) | 1] = (uint8_t)((a & 1) ? v >> 8 : v);
}

static void data_write(uint16_t v)
{
    switch (V.code & 0x0F) {
    case VDP_CODE_VRAM_WR:  vram_wr16(V.addr, v); break;
    case VDP_CODE_CRAM_WR:  V.cram[(V.addr >> 1) & 0x3F] = v & 0x0EEE; break;
    case VDP_CODE_VSRAM_WR:
        if (((V.addr >> 1) & 0x3F) < 40) V.vsram[(V.addr >> 1) & 0x3F] = v & 0x07FF;
        break;
    default:
        nmd_fatal("VDP data-port write with read/invalid code %02X (addr %04X)", V.code, V.addr);
    }
    V.addr = (uint16_t)(V.addr + autoinc());
}

static uint16_t data_read(void)
{
    uint16_t v;
    switch (V.code & 0x0F) {
    case VDP_CODE_VRAM_RD:  v = be16(V.vram + (V.addr & 0xFFFE)); break;
    case VDP_CODE_CRAM_RD:  v = V.cram[(V.addr >> 1) & 0x3F]; break;
    case VDP_CODE_VSRAM_RD: v = V.vsram[((V.addr >> 1) & 0x3F) % 40]; break;
    default:
        nmd_fatal("VDP data-port read with write/invalid code %02X (addr %04X)", V.code, V.addr);
        return 0;
    }
    V.addr = (uint16_t)(V.addr + autoinc());
    return v;
}

/* ------------------------------------------------------------------------ */
/* DMA                                                                       */
/* ------------------------------------------------------------------------ */

static unsigned dma_length(void)
{
    unsigned len = V.reg[19] | (unsigned)V.reg[20] << 8;
    return len ? len : 0x10000;
}

static void dma_set_length_done(void)
{
    /* After a DMA the length counter reads zero. */
    V.reg[19] = V.reg[20] = 0;
}

static void dma_68k(void)
{
    uint32_t src = ((uint32_t)V.reg[21] << 1) | ((uint32_t)V.reg[22] << 9) |
                   ((uint32_t)(V.reg[23] & 0x7F) << 17);
    unsigned len = dma_length();
    for (unsigned i = 0; i < len; i++) {
        /* The source wraps inside a 128 KB block. */
        uint16_t w = nmd_bus_rd16_raw(src);
        data_write(w);
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
    }
    src >>= 1;
    V.reg[21] = (uint8_t)src;
    V.reg[22] = (uint8_t)(src >> 8);
    dma_set_length_done();
}

static void dma_fill(uint16_t v)
{
    /* The data-port write that armed the fill has already been done. */
    unsigned len = dma_length();
    if ((V.code & 0x0F) != VDP_CODE_VRAM_WR)
        nmd_fatal("VDP DMA fill to CRAM/VSRAM is not supported yet (code %02X)", V.code);
    for (unsigned i = 0; i < len; i++) {
        V.vram[V.addr ^ 1] = (uint8_t)(v >> 8);
        V.addr = (uint16_t)(V.addr + autoinc());
    }
    dma_set_length_done();
}

static void dma_copy(void)
{
    uint16_t src = (uint16_t)(V.reg[21] | V.reg[22] << 8);
    unsigned len = dma_length();
    for (unsigned i = 0; i < len; i++) {
        V.vram[V.addr ^ 1] = V.vram[src ^ 1];
        src++;
        V.addr = (uint16_t)(V.addr + autoinc());
    }
    V.reg[21] = (uint8_t)src;
    V.reg[22] = (uint8_t)(src >> 8);
    dma_set_length_done();
}

static void dma_start(void)
{
    if (!(V.reg[1] & 0x10)) return; /* DMA disabled: plain command */
    switch (V.reg[23] >> 6) {
    case 0: case 1: dma_68k(); break;
    case 2:         V.fill_pending = true; break;
    case 3:         dma_copy(); break;
    }
}

/* ------------------------------------------------------------------------ */
/* Ports                                                                     */
/* ------------------------------------------------------------------------ */

static void ctrl_write(uint16_t w)
{
    if (!V.cmd_pending) {
        if ((w & 0xC000) == 0x8000) {
            unsigned r = (w >> 8) & 0x1F;
            if (r < 24) V.reg[r] = (uint8_t)w;
            return;
        }
        V.code = (uint8_t)((V.code & 0x3C) | (w >> 14));
        V.addr = (uint16_t)((V.addr & 0xC000) | (w & 0x3FFF));
        V.cmd_pending = true;
        return;
    }
    V.cmd_pending = false;
    V.code = (uint8_t)((V.code & 0x03) | ((w >> 2) & 0x3C));
    V.addr = (uint16_t)((V.addr & 0x3FFF) | ((w & 3) << 14));
    if (V.code & VDP_CODE_DMA) dma_start();
}

static uint16_t status(void)
{
    uint16_t s = VDP_STATUS_EMPTY;
    if (V.in_vblank || !(V.reg[1] & 0x40)) s |= VDP_STATUS_VBLANK;
    if (V.vint_pending) s |= VDP_STATUS_VINT;
    if (NMD.cfg.video == NMD_VIDEO_PAL) s |= VDP_STATUS_PAL;
    return s;
}

uint16_t vdp_rd16(uint32_t addr)
{
    switch (addr & 0x1E) {
    case 0x00: case 0x02:
        V.cmd_pending = false;
        return data_read();
    case 0x04: case 0x06:
        V.cmd_pending = false;
        return status();
    case 0x08: case 0x0A: case 0x0C: case 0x0E:
        /* HV counter: coarse, phase-based (contract "VDP timing"). */
        return V.in_vblank ? (uint16_t)(active_lines() << 8) : 0;
    }
    nmd_fatal("VDP read from unsupported port %06X", addr);
    return 0;
}

void vdp_wr16(uint32_t addr, uint16_t v)
{
    switch (addr & 0x1E) {
    case 0x00: case 0x02:
        V.cmd_pending = false;
        data_write(v);
        if (V.fill_pending) {
            V.fill_pending = false;
            dma_fill(v);
        }
        return;
    case 0x04: case 0x06:
        ctrl_write(v);
        return;
    }
    nmd_fatal("VDP write to unsupported port %06X <- %04X", addr, v);
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                     */
/* ------------------------------------------------------------------------ */

unsigned vdp_active_lines(void) { return active_lines(); }

void vdp_begin_frame(void)
{
    V.in_vblank = false;
    V.hint_counter = V.reg[10];
}

bool vdp_line(unsigned line)
{
    (void)line; /* TODO(milestone 4): render the line here. */
    if (V.hint_counter-- == 0) {
        V.hint_counter = V.reg[10];
        return (V.reg[0] & 0x10) != 0;
    }
    return false;
}

void vdp_end_active(void)
{
    V.in_vblank = true;
    V.vint_pending = true;
}

void vdp_frame(void (*hint)(void))
{
    unsigned lines = active_lines();
    vdp_begin_frame();
    for (unsigned line = 0; line < lines; line++)
        if (vdp_line(line) && hint) hint();
    vdp_end_active();
}
