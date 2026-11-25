/*
 * nativemd.h - Engine API: create the machine, load a ROM, run frames.
 */
#ifndef NATIVEMD_H
#define NATIVEMD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "machine.h"
#include "z80.h"
#include "hw.h"
#include "cart.h"
#include "host.h"
#include "trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Contract version. 0.x is a draft; 1.0 will be frozen (backward compatible). */
#define NMD_CONTRACT_VERSION_MAJOR 0
#define NMD_CONTRACT_VERSION_MINOR 1

typedef enum nmd_video {
    NMD_VIDEO_NTSC = 0, /* 60 Hz, 262 lines */
    NMD_VIDEO_PAL  = 1, /* 50 Hz, 313 lines */
} nmd_video;

/*
 * 68000 writes to ROM that no cartridge chip claims (contract section 3). On hardware
 * without a mapper they have no effect (to verify). Either way they are traced as
 * ordinary accesses.
 */
typedef enum nmd_rom_writes {
    NMD_ROM_WRITES_IGNORE = 0, /* no effect; counted (nmd_rom_write_sites) */
    NMD_ROM_WRITES_FATAL  = 1, /* strict: fatal, with the origin and the registers */
} nmd_rom_writes;

typedef struct nmd_config {
    nmd_video      video;
    bool           overseas;   /* version register bit 7 */
    nmd_host       host;
    nmd_rom_writes rom_writes;
} nmd_config;

/*
 * Initialise the (single, global) machine with a ROM image and a game.
 * The ROM buffer is copied. Returns false on error (message on stderr).
 */
bool nmd_init(const nmd_config *cfg, const nmd_game *game,
              const uint8_t *rom, size_t rom_size);
void nmd_shutdown(void);

const nmd_cart_info *nmd_cart(void);

/*
 * Run until `frames` more frames have completed (each completed frame ends
 * after VINT). The game's reset routine is started on the first call.
 * Returns the number of frames completed in total.
 */
uint64_t nmd_run_frames(uint64_t frames);
uint64_t nmd_frame_count(void);

/* True once the game's main routine has returned (normally never). */
bool nmd_game_finished(void);

/* Fatal errors. The default handler prints the message and calls abort(). */
typedef void (*nmd_fatal_handler)(const char *msg);
void nmd_set_fatal_handler(nmd_fatal_handler handler);
void nmd_fatal(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* Direct views of machine memory (for the harness and headless dumps). */
uint8_t       *nmd_ram(void);        /* 64 KB  */
uint8_t       *nmd_vram(void);       /* 64 KB  */
uint16_t      *nmd_cram(void);       /* 64 words, 0000BBB0GGG0RRR0 */
uint16_t      *nmd_vsram(void);      /* 40 words */
uint8_t       *nmd_vdp_regs(void);   /* 24 bytes */
uint8_t       *nmd_z80_ram(void);    /* 8 KB   */
const uint8_t *nmd_rom(size_t *size);

/*
 * Machine state snapshot (docs/harness.md, "State files"): CPU registers,
 * RAM, VRAM, CRAM, VSRAM, VDP registers, Z80 RAM, and the VDP / I/O / Z80
 * latches. Used to capture the inputs of a routine on the oracle and replay
 * them on the candidate.
 */
typedef struct nmd_state_image {
    m68k_regs regs;
    uint8_t   ram[0x10000];
    uint8_t   vram[0x10000];
    uint16_t  cram[64];
    uint16_t  vsram[40];
    uint8_t   vdp_reg[24];
    uint8_t   z80_ram[0x2000];
    /* latches (state version 2) */
    uint8_t   vdp_code;
    bool      vdp_cmd_pending, vdp_fill_pending, vdp_in_vblank, vdp_vint_pending;
    uint16_t  vdp_addr, vdp_read_buffer;
    int16_t   vdp_hint_counter;
    uint8_t   io_data[3], io_ctrl[3];
    uint16_t  io_pad[2];
    bool      z80_busreq, z80_reset;
    uint8_t   psg_last;
    uint32_t  z80_bank;
    uint8_t   ym_addr[2];
    uint8_t   ym_reg[2][256];
} nmd_state_image;

bool nmd_state_save(const char *path);   /* capture + write */
bool nmd_state_load(const char *path);   /* read + apply    */
void nmd_state_capture(nmd_state_image *out);
void nmd_state_apply(const nmd_state_image *in);
bool nmd_state_write(const char *path, const nmd_state_image *in);
bool nmd_state_read(const char *path, nmd_state_image *out); /* versions 1 and 2 */

/*
 * Routines without C. When call_routine / jump_routine, an interrupt or the
 * reset vector reaches an address with no C, the handler is asked first; it
 * returns true if it ran the routine (the harness uses this for hybrid
 * execution on the oracle CPU). Without a handler, or if it returns false,
 * the miss is fatal. The engine itself never emulates a CPU.
 */
typedef bool (*nmd_missing_routine_handler)(uint32_t addr, bool is_interrupt);
void nmd_set_missing_routine_handler(nmd_missing_routine_handler handler);

/*
 * Side-effect read replay (docs/harness.md). While active, every read the C
 * makes from a device (Z80 space, I/O, system control, VDP: $A00000-$DFFFFF)
 * must match the next device read of the recorded trace in size and address;
 * the device is still read (for its side effects) but the recorded value is
 * returned. A mismatch or running out of records is fatal.
 */
bool   nmd_replay_open(FILE *recorded_trace);
void   nmd_replay_close(void);
size_t nmd_replay_remaining(void);

/*
 * Ignored ROM writes (NMD_ROM_WRITES_IGNORE), one site per address, in order of first
 * write. The origin of the first write is the 68000 PC of the instruction when the
 * oracle CPU made it, or the innermost C routine's address when C made it. Cleared by
 * nmd_init() and nmd_rom_writes_clear(). Addresses beyond NMD_MAX_ROM_WRITE_SITES are
 * counted together in *other (never dropped).
 */
#define NMD_MAX_ROM_WRITE_SITES 256

typedef struct nmd_rom_write_site {
    uint32_t addr;
    uint64_t count;
    unsigned first_bits;    /* 8 or 16 */
    uint16_t first_value;
    bool     first_from_c;  /* origin: C routine address, else 68000 PC */
    bool     first_known;   /* false: neither (e.g. a write outside any routine) */
    uint32_t first_origin;
} nmd_rom_write_site;

size_t   nmd_rom_write_sites(const nmd_rom_write_site **sites, uint64_t *other);
uint64_t nmd_rom_write_total(void);
void     nmd_rom_writes_clear(void);
/* Prints "who: N ignored ROM write(s) at M address(es)" and one line per site; nothing
 * when there were none. */
void     nmd_rom_writes_report(FILE *f, const char *who);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_H */
