/*
 * nativemd.h - Engine API: create the machine, load a ROM, run frames.
 */
#ifndef NATIVEMD_H
#define NATIVEMD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

typedef struct nmd_config {
    nmd_video video;
    bool      overseas;   /* version register bit 7 */
    nmd_host  host;
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
 * RAM, VRAM, CRAM, VSRAM, VDP registers, Z80 RAM. Used to capture the inputs
 * of a routine on the oracle and replay them on the candidate.
 */
bool nmd_state_save(const char *path);
bool nmd_state_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_H */
