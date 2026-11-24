/* internal.h - shared engine state (not part of the public contract). */
#ifndef NMD_INTERNAL_H
#define NMD_INTERNAL_H

#include "nativemd/nativemd.h"

typedef struct nmd_vdp {
    uint8_t  vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[40];
    uint8_t  reg[24];

    bool     cmd_pending;  /* first control word latched */
    uint8_t  code;         /* CD5-CD0 */
    uint16_t addr;
    bool     fill_pending; /* VRAM fill armed, waits for a data-port write */
    uint16_t read_buffer;

    bool     in_vblank;    /* scheduler phase, see contract "VDP timing" */
    bool     vint_pending;
    int      hint_counter;
} nmd_vdp;

typedef struct nmd_io {
    uint8_t  data[3];
    uint8_t  ctrl[3];
    uint16_t pad[2];       /* NMD_PAD_* sampled for this frame */
} nmd_io;

typedef struct nmd_z80 {
    uint8_t  ram[0x2000];
    bool     busreq;       /* 68000 holds the Z80 bus */
    bool     reset;        /* Z80 held in reset */
    uint32_t bank;         /* 68000 address of the 8000-FFFF window */
    unsigned bank_shift;
    uint8_t  ym_addr[2];
    uint8_t  ym_reg[2][256];
    uint8_t  psg_last;
} nmd_z80;

typedef struct nmd_machine {
    nmd_config      cfg;
    const nmd_game *game;
    uint8_t        *rom;
    size_t          rom_size;
    nmd_cart_info   cart;
    uint8_t         ram[0x10000];
    nmd_vdp         vdp;
    nmd_io          io;
    nmd_z80         z80;
    nmd_chip        chips[NMD_MAX_CHIPS];
    size_t          chip_count;
    uint64_t        frame;
    bool            started;
    bool            finished;
} nmd_machine;

extern nmd_machine NMD;

/* bus.c */
uint16_t nmd_bus_rd16_raw(uint32_t addr); /* no trace; used by DMA */

/* vdp.c */
void     vdp_reset(void);
uint16_t vdp_rd16(uint32_t addr);
void     vdp_wr16(uint32_t addr, uint16_t v);
void     vdp_frame(void (*hint)(void)); /* active display: line loop + HINT */
bool     vdp_vint_enabled(void);

/* io.c */
void    io_reset(void);
uint8_t io_rd8(uint32_t addr);
void    io_wr8(uint32_t addr, uint8_t v);

/* z80.c */
void     z80_reset_state(void);
uint8_t  z80_area_rd8(uint32_t addr);
void     z80_area_wr8(uint32_t addr, uint8_t v);
uint16_t z80_ctrl_rd16(uint32_t addr);
void     z80_ctrl_wr16(uint32_t addr, uint16_t v);
void     psg_write(uint8_t v);

/* Big-endian helpers */
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

#endif
