/*
 * io.c - Version register and controller ports (A10000-A1001F).
 *
 * Implemented: 3-button pad on ports 1 and 2 (TH select).
 * TODO(milestone 5 of the console plan): 6-button pad, mouse, multitap.
 */
#include "internal.h"

#define IO (NMD.io)

void io_reset(void)
{
    for (int i = 0; i < 3; i++) {
        IO.data[i] = 0x7F;
        IO.ctrl[i] = 0x00;
    }
    IO.pad[0] = IO.pad[1] = 0;
}

static uint8_t pad_lines(int port)
{
    uint16_t b = IO.pad[port];
    bool th = (IO.data[port] & 0x40) != 0;
    uint8_t v;
    /* Active low. TH=1: ?1CBRLDU, TH=0: ?0SA00DU */
    if (th) {
        v = 0x40 | 0x3F;
        if (b & NMD_PAD_UP)    v &= ~0x01;
        if (b & NMD_PAD_DOWN)  v &= ~0x02;
        if (b & NMD_PAD_LEFT)  v &= ~0x04;
        if (b & NMD_PAD_RIGHT) v &= ~0x08;
        if (b & NMD_PAD_B)     v &= ~0x10;
        if (b & NMD_PAD_C)     v &= ~0x20;
    } else {
        v = 0x33;
        if (b & NMD_PAD_UP)    v &= ~0x01;
        if (b & NMD_PAD_DOWN)  v &= ~0x02;
        if (b & NMD_PAD_A)     v &= ~0x10;
        if (b & NMD_PAD_START) v &= ~0x20;
    }
    return v;
}

uint8_t io_rd8(uint32_t addr)
{
    if (!(addr & 1)) nmd_fatal("I/O read8 at even address %06X", addr);
    unsigned reg = (addr & 0x1F) >> 1;
    switch (reg) {
    case 0: { /* version */
        uint8_t v = 0x20; /* no expansion unit, hardware version 0 */
        if (NMD.cfg.overseas)                 v |= 0x80;
        if (NMD.cfg.video == NMD_VIDEO_PAL)   v |= 0x40;
        return v;
    }
    case 1: case 2: case 3: {
        int port = (int)reg - 1;
        uint8_t in  = port < 2 ? pad_lines(port) : 0x7F;
        uint8_t out = IO.ctrl[port];
        /* Output pins read back the latch, input pins read the device. */
        return (uint8_t)((IO.data[port] & 0x80) | (IO.data[port] & out & 0x7F) | (in & ~out & 0x7F));
    }
    case 4: case 5: case 6:
        return IO.ctrl[reg - 4];
    }
    /* Serial registers: not connected. */
    return 0;
}

void io_wr8(uint32_t addr, uint8_t v)
{
    if (!(addr & 1)) return; /* even halves are not connected */
    unsigned reg = (addr & 0x1F) >> 1;
    switch (reg) {
    case 1: case 2: case 3: IO.data[reg - 1] = v; return;
    case 4: case 5: case 6: IO.ctrl[reg - 4] = v; return;
    default: return; /* version is read-only; serial ignored */
    }
}
