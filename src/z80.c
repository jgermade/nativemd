/*
 * z80.c - Z80 memory, bus arbitration and the 68000 window.
 *
 * Implemented: Z80 RAM, BUSREQ/RESET, bank register, z80_rd/z80_wr,
 * YM2612 register latching, PSG write capture.
 * TODO(milestone 5): Z80 coroutine with its own clock; z80_wait_*.
 * TODO(milestone 6): PSG and YM2612 synthesis.
 */
#include <string.h>

#include "internal.h"

#define Z (NMD.z80)

z80_regs Z80;

void z80_reset_state(void)
{
    memset(&Z, 0, sizeof Z);
    memset(&Z80, 0, sizeof Z80);
    Z.reset = true;  /* the Z80 starts held in reset */
}

/* ------------------------------------------------------------------------ */
/* Shared devices                                                            */
/* ------------------------------------------------------------------------ */

static uint8_t ym_read(void)
{
    return 0x00; /* status: not busy, no timer overflow (timers: milestone 6) */
}

static void ym_write(unsigned port, uint8_t v)
{
    unsigned part = (port >> 1) & 1;
    if (port & 1) Z.ym_reg[part][Z.ym_addr[part]] = v;
    else          Z.ym_addr[part] = v;
}

void psg_write(uint8_t v)
{
    Z.psg_last = v;
}

static void bank_write(uint8_t v)
{
    /* Serial: each write shifts bit 0 into A15..A23 (LSB first). */
    Z.bank = (Z.bank >> 1) | ((uint32_t)(v & 1) << 23);
    Z.bank &= 0xFF8000;
}

/* ------------------------------------------------------------------------ */
/* 68000 side                                                                */
/* ------------------------------------------------------------------------ */

static void require_bus(uint32_t addr, const char *what)
{
    /* Policy: the 68000 must hold BUSREQ. (Access while only in reset: to verify.) */
    if (!Z.busreq)
        nmd_fatal("68k %s Z80 space %06X without holding the Z80 bus", what, addr);
}

uint8_t z80_area_rd8(uint32_t addr)
{
    require_bus(addr, "read8 from");
    uint16_t a = (uint16_t)(addr & 0x7FFF);
    if (a < 0x4000) return Z.ram[a & 0x1FFF];
    if (a < 0x6000) return ym_read();
    nmd_fatal("68k read8 from unsupported Z80-space address %06X", addr);
    return 0;
}

void z80_area_wr8(uint32_t addr, uint8_t v)
{
    require_bus(addr, "write8 to");
    uint16_t a = (uint16_t)(addr & 0x7FFF);
    if (a < 0x4000)        { Z.ram[a & 0x1FFF] = v; return; }
    if (a < 0x6000)        { ym_write(a & 3, v); return; }
    if (a < 0x6100)        { bank_write(v); return; }
    if (a == 0x7F11)       { psg_write(v); return; }
    nmd_fatal("68k write8 to unsupported Z80-space address %06X <- %02X", addr, v);
}

uint16_t z80_ctrl_rd16(uint32_t addr)
{
    if (addr == HW_Z80_BUSREQ) {
        /* Bit 0 (bit 8 of the word) reads 0 when the bus is granted. Grant is immediate. */
        return Z.busreq ? 0x0000 : 0x0100;
    }
    return 0x0000; /* reset register reads are undefined; return 0 */
}

void z80_ctrl_wr16(uint32_t addr, uint16_t v)
{
    bool bit = (v & 0x0100) != 0;
    if (addr == HW_Z80_BUSREQ) Z.busreq = bit;
    else                       Z.reset = !bit; /* write 0 = assert reset */
}

/* ------------------------------------------------------------------------ */
/* Z80 side (contract z80.h)                                                 */
/* ------------------------------------------------------------------------ */

uint8_t z80_rd(uint16_t addr)
{
    uint8_t v;
    if (addr < 0x4000)        v = Z.ram[addr & 0x1FFF];
    else if (addr < 0x6000)   v = ym_read();
    else if (addr >= 0x8000) {
        if (Z.busreq) nmd_fatal("Z80 read from 68k window while the 68k holds the Z80 bus");
        uint32_t a = Z.bank | (addr & 0x7FFF);
        if (a >= 0xA00000 && a <= 0xA0FFFF) nmd_fatal("Z80 window access to Z80 space %06X", a);
        uint16_t w = nmd_bus_rd16_raw(a & ~1u);
        v = (addr & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    } else {
        nmd_fatal("Z80 read from unsupported address %04X", addr);
        v = 0;
    }
    nmd_trace_access(NMD_TRACE_Z80, false, 8, addr, v);
    return v;
}

void z80_wr(uint16_t addr, uint8_t v)
{
    nmd_trace_access(NMD_TRACE_Z80, true, 8, addr, v);
    if (addr < 0x4000)        { Z.ram[addr & 0x1FFF] = v; return; }
    if (addr < 0x6000)        { ym_write(addr & 3, v); return; }
    if (addr < 0x6100)        { bank_write(v); return; }
    if (addr == 0x7F11)       { psg_write(v); return; }
    if (addr >= 0x8000)
        nmd_fatal("Z80 write to 68k window (%04X <- %02X) is not supported yet", addr, v);
    nmd_fatal("Z80 write to unsupported address %04X <- %02X", addr, v);
}

void z80_wait_until(bool (*cond)(void *user), void *user)
{
    (void)cond; (void)user;
    nmd_fatal("z80_wait_until: Z80 scheduling is not implemented yet (milestone 5)");
}

void z80_wait_vblank(void)
{
    nmd_fatal("z80_wait_vblank: Z80 scheduling is not implemented yet (milestone 5)");
}

void z80_call_routine(uint16_t addr)
{
    nmd_fatal("z80_call_routine(%04X): Z80 routine tables are not implemented yet", addr);
}

void z80_jump_routine(uint16_t addr)
{
    nmd_fatal("z80_jump_routine(%04X): Z80 routine tables are not implemented yet", addr);
}
