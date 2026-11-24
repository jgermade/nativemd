/*
 * z80.h - Z80 side of the nativemd contract.
 *
 * The sound driver (usually a blob copied into Z80 RAM by the 68000) is
 * decompiled to C against this header, the same way the 68000 program is
 * written against machine.h. Draft: contract 0.x, not frozen.
 *
 * Z80 memory map as seen by z80_rd / z80_wr:
 *   0000-1FFF  Z80 RAM (8 KB), mirrored at 2000-3FFF
 *   4000-4003  YM2612 (address/data, part I and II)
 *   6000       bank register (serial, 9 writes of bit 0)
 *   7F11       SN76489 PSG
 *   8000-FFFF  window into the 68000 bus (32 KB, selected by the bank)
 */
#ifndef NATIVEMD_Z80_H
#define NATIVEMD_Z80_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct z80_regs {
    uint8_t  a, f, b, c, d, e, h, l;
    uint8_t  a_, f_, b_, c_, d_, e_, h_, l_; /* shadow set */
    uint16_t ix, iy, sp;
    uint8_t  i, r;
    bool     iff1, iff2;
    uint8_t  im;
} z80_regs;

extern z80_regs Z80;

uint8_t z80_rd(uint16_t addr);
void    z80_wr(uint16_t addr, uint8_t value);

/*
 * Waiting. The Z80 program runs in its own coroutine with its own clock.
 * z80_wait_until yields until cond(user) is true; z80_wait_vblank yields
 * until the next Z80 interrupt (raised once per frame at VBlank).
 * (Coroutine scheduling is milestone 5; these are declared now so drivers
 * can be written against the final API.)
 */
void z80_wait_until(bool (*cond)(void *user), void *user);
void z80_wait_vblank(void);

/* Routines reached by address inside Z80 space (jump tables, RST vectors). */
typedef void (*z80_routine_fn)(void);
void z80_call_routine(uint16_t addr);
void z80_jump_routine(uint16_t addr); /* caller must return right after */

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_Z80_H */
