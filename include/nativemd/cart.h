/*
 * cart.h - Cartridge description and the chip/expansion connector.
 */
#ifndef NATIVEMD_CART_H
#define NATIVEMD_CART_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmd_region {
    NMD_REGION_JAPAN  = 1 << 0, /* NTSC, domestic */
    NMD_REGION_USA    = 1 << 1, /* NTSC, overseas */
    NMD_REGION_EUROPE = 1 << 2, /* PAL, overseas  */
} nmd_region;

typedef enum nmd_mapper {
    NMD_MAPPER_NONE = 0,  /* plain ROM up to 4 MB                         */
    NMD_MAPPER_SSF2,      /* Sega mapper (A130F3-A130FF), not implemented  */
} nmd_mapper;

/* Information parsed from the ROM header ($100-$1FF). */
typedef struct nmd_cart_info {
    char     system[17];
    char     copyright[17];
    char     name_domestic[49];
    char     name_overseas[49];
    char     serial[15];
    uint16_t checksum_header;
    uint16_t checksum_actual;
    uint32_t rom_start, rom_end;
    bool     has_sram;
    uint8_t  sram_type;           /* byte at $1B2 */
    uint32_t sram_start, sram_end;
    unsigned regions;             /* nmd_region bit set */
} nmd_cart_info;

/*
 * Chip connector. A chip claims an address range on the 68000 bus and
 * handles accesses to it before the default memory map is consulted. This is
 * where SRAM, mappers, and later expansions (Sega CD, 32X) plug in.
 * Callbacks may be NULL: a NULL callback in a claimed range is fatal.
 */
typedef struct nmd_chip {
    const char *name;
    uint32_t    start, end;  /* inclusive 24-bit range */
    void       *user;
    uint8_t   (*rd8)(void *user, uint32_t addr);
    uint16_t  (*rd16)(void *user, uint32_t addr);
    void      (*wr8)(void *user, uint32_t addr, uint8_t v);
    void      (*wr16)(void *user, uint32_t addr, uint16_t v);
    void      (*reset)(void *user);
    void      (*frame)(void *user); /* called once per frame, after VINT */
} nmd_chip;

#define NMD_MAX_CHIPS 8

/* One entry of the game's routine table. */
typedef struct nmd_routine {
    uint32_t       addr;
    nmd_routine_fn fn;
    const char    *name;
} nmd_routine;

/*
 * What a game links in: its name, its routine table (which must contain the
 * reset PC and every interrupt handler it enables) and optional chips.
 * The ROM image itself is supplied by the user at run time (never shipped).
 */
typedef struct nmd_game {
    const char        *name;
    const nmd_routine *routines;
    size_t             routine_count;
    const nmd_chip    *chips;
    size_t             chip_count;
    nmd_mapper         mapper;
} nmd_game;

/* Parse the header of a ROM image. Returns false if the image is too small. */
bool nmd_cart_parse(const uint8_t *rom, size_t size, nmd_cart_info *out);

/* Sum of big-endian words from $200 to the end (the header checksum rule). */
uint16_t nmd_cart_checksum(const uint8_t *rom, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_CART_H */
