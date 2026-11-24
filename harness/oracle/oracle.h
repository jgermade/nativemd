/*
 * oracle.h - the original 68000 code on Musashi, with the nativemd hardware
 * model behind it (docs/harness.md, "Oracle").
 *
 * Call nmd_init() first, then oracle_setup(). The oracle uses the engine's
 * machine (memory, VDP, I/O, Z80 area) and the engine's trace sink.
 */
#ifndef NMD_ORACLE_H
#define NMD_ORACLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Reset Musashi, install callbacks, default filters (stack = 1 KB below the
 * initial SSP, no idle loop). */
void oracle_setup(void);

/* Trace filters. */
void oracle_set_stack(uint32_t lo, uint32_t hi);        /* inclusive */
void oracle_set_idle(uint32_t lo, uint32_t hi);         /* [lo, hi)  */

/* Symbols ("ADDRESS NAME" lines). resolve() accepts a name or a hex address;
 * *next receives the next symbol's address when there is one. */
bool oracle_load_syms(const char *path);
bool oracle_resolve(const char *text, uint32_t *addr, uint32_t *next, bool *has_next);

/* Frames mode inputs and outputs. */
void oracle_set_pad_script(const uint16_t (*script)[2], size_t frames);
void oracle_enable_coverage(void);
bool oracle_write_coverage(const char *path);
/* Save a state each time PC reaches addr (at most max per address) as
 * DIR/NAME.NNNN.nmds. */
bool oracle_add_capture(uint32_t addr, const char *name);
void oracle_set_capture(const char *dir, unsigned max_per_addr);
unsigned oracle_captured(void);

/* Reset and run with NTSC line timing until the RTE of the N-th VINT. */
bool oracle_run_frames(uint64_t frames);

/*
 * Lag frames of the last oracle_run_frames (needs an idle loop): VINTs taken
 * before the main code had polled the idle loop since the previous VINT. The
 * engine's timing model has none, so full-run comparisons diverge there.
 */
uint64_t oracle_lag_frames(uint64_t *first_vint);

/*
 * Run the original routine at addr from the current machine state and M68K
 * registers until it returns (RTS, or RTE when irq is true); M68K receives
 * the final registers. No interrupts; never diverts into C.
 */
bool oracle_call(uint32_t addr, bool irq, uint64_t max_cycles);

/*
 * Hybrid execution: routines without C run on Musashi; when that code reaches
 * an address that has C, the C runs instead (and returns as by RTS). An ASM
 * idle loop set with oracle_set_idle() yields to the engine (wait_vblank) on
 * its second pass. Installs itself as the engine's missing-routine handler.
 */
void oracle_install_hybrid(uint64_t max_cycles_per_slice);

#endif
