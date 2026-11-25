/*
 * machine.h - 68000 side of the nativemd contract.
 *
 * Game C code (a decompilation of the original 68000 program) is written
 * against this header. It never touches hardware directly: every data access
 * to the 68000 bus goes through m68k_rd* / m68k_wr*, so the engine can model
 * the hardware and the harness can record and compare bus traces.
 *
 * See docs/contract.md for the full rules. Draft: contract 0.x, not frozen.
 */
#ifndef NATIVEMD_MACHINE_H
#define NATIVEMD_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* CPU register file                                                         */
/* ------------------------------------------------------------------------ */

/*
 * Register state shared between the engine, abi_ wrappers and the harness.
 * Routines that the original program calls with arguments in registers read
 * them from here (via their abi_ wrapper) and leave results here.
 */
typedef struct m68k_regs {
    uint32_t d[8];
    uint32_t a[8]; /* a[7] is the active stack pointer */
    uint16_t sr;   /* T S I2 I1 I0 X N Z V C */
} m68k_regs;

extern m68k_regs M68K;

#define M68K_SR_C      0x0001u
#define M68K_SR_V      0x0002u
#define M68K_SR_Z      0x0004u
#define M68K_SR_N      0x0008u
#define M68K_SR_X      0x0010u
#define M68K_SR_IMASK  0x0700u
#define M68K_SR_S      0x2000u

/* ------------------------------------------------------------------------ */
/* Bus access                                                                */
/* ------------------------------------------------------------------------ */

/*
 * Addresses are 24-bit; upper bits are ignored.
 * 16- and 32-bit accesses to odd addresses are a fatal error (the 68000 would
 * raise an address error; see contract "Address errors").
 * 32-bit accesses are performed and traced as two 16-bit accesses, high word
 * first.
 */
uint8_t  m68k_rd8(uint32_t addr);
uint16_t m68k_rd16(uint32_t addr);
uint32_t m68k_rd32(uint32_t addr);

void m68k_wr8(uint32_t addr, uint8_t value);
void m68k_wr16(uint32_t addr, uint16_t value);
void m68k_wr32(uint32_t addr, uint32_t value);

/* MOVE.L to -(An): the low word is written first. */
void m68k_wr32_predec(uint32_t addr, uint32_t value);

/* ------------------------------------------------------------------------ */
/* Read-modify-write and instructions with special bus behaviour             */
/* ------------------------------------------------------------------------ */

/* CLR to memory: the 68000 reads the operand before writing zero. */
void m68k_clr8(uint32_t addr);
void m68k_clr16(uint32_t addr);
void m68k_clr32(uint32_t addr);

/*
 * TAS: returns the byte read (the caller sets N/Z from it). On the Mega
 * Drive the write-back cycle of TAS does not reach memory; the engine models
 * that by performing the read only. (To be confirmed against the oracle.)
 */
uint8_t m68k_tas(uint32_t addr);

/* Generic RMW helpers (ADDQ/SUBQ/OR/AND/EOR/NOT/NEG ... to memory). */
#define M68K_RMW8(addr, expr_of_v)  do { uint32_t a_ = (addr); uint8_t  v = m68k_rd8(a_);  m68k_wr8(a_,  (uint8_t)(expr_of_v)); } while (0)
#define M68K_RMW16(addr, expr_of_v) do { uint32_t a_ = (addr); uint16_t v = m68k_rd16(a_); m68k_wr16(a_, (uint16_t)(expr_of_v)); } while (0)
#define M68K_RMW32(addr, expr_of_v) do { uint32_t a_ = (addr); uint32_t v = m68k_rd32(a_); m68k_wr32(a_, (uint32_t)(expr_of_v)); } while (0)

/* ------------------------------------------------------------------------ */
/* Waiting                                                                   */
/* ------------------------------------------------------------------------ */

/*
 * Busy-wait loops on hardware state (VBlank flags set by the VINT handler,
 * VDP status polling, ...) must be written as explicit waits. The engine
 * runs the rest of the machine until the condition holds.
 */

/* Yield until the start of the next vertical blank (after VINT has run). */
void wait_vblank(void);

/* Yield frame by frame until cond(user) returns true. Checked once per frame. */
void wait_until(bool (*cond)(void *user), void *user);

/* ------------------------------------------------------------------------ */
/* Routines called by address                                                */
/* ------------------------------------------------------------------------ */

typedef void (*nmd_routine_fn)(void);

/*
 * Every 68000 address that can be reached indirectly (vectors, jump tables,
 * pointers stored in data) maps to a C function through the game's routine
 * table (see nmd_game in cart.h). Routines are usually abi_ wrappers that
 * take their arguments from M68K and store results back.
 *
 * A missing address is a fatal error, never a silent fallback.
 */
void call_routine(uint32_t addr);   /* JSR/BSR to an address held in data   */

/*
 * JMP to an address held in data. C has no tail calls across function
 * pointers, so the caller must return right after: `jump_routine(a); return;`
 */
void jump_routine(uint32_t addr);

/* Lookup without calling; returns NULL if the address has no C. */
nmd_routine_fn find_routine(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_MACHINE_H */
