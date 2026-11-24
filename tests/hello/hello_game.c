/*
 * hello_game.c - the "decompiled" C of the hello ROM, written against the
 * contract exactly as a real game decompilation would be.
 */
#include <stddef.h>

#include "nativemd/nativemd.h"
#include "hello_layout.h"

/* ---- abi_ wrappers: routines reachable by address ------------------------ */

static void hello_main(void);
static void hello_hint(void);
static void hello_vint(void);
static void hello_error(void);

static const nmd_routine routines[] = {
    { HELLO_ADDR_MAIN,  hello_main,  "Main"  },
    { HELLO_ADDR_HINT,  hello_hint,  "HInt"  },
    { HELLO_ADDR_VINT,  hello_vint,  "VInt"  },
    { HELLO_ADDR_ERROR, hello_error, "Error" },
};

const nmd_game hello_game = {
    .name = "hello",
    .routines = routines,
    .routine_count = sizeof routines / sizeof routines[0],
};

/* ---- helpers --------------------------------------------------------------- */

static void vdp_set_reg(unsigned r, unsigned v) { m68k_wr16(HW_VDP_CTRL, VDP_REG(r, v)); }

static bool vflag_clear(void *user)
{
    (void)user;
    return m68k_rd8(HELLO_RAM_VFLAG) == 0;
}

/* ---- routines -------------------------------------------------------------- */

static void z80_init(void)
{
    m68k_wr16(HW_Z80_BUSREQ, 0x0100);
    m68k_wr16(HW_Z80_RESET, 0x0100);
    while (m68k_rd16(HW_Z80_BUSREQ) & 0x0100) {
        /* granted immediately by the engine; loop kept as in the original */
    }
    m68k_wr8(0xA00000, 0xF3); /* di   */
    m68k_wr8(0xA00001, 0x76); /* halt */
    m68k_wr16(HW_Z80_RESET, 0x0000);
    m68k_wr16(HW_Z80_BUSREQ, 0x0000);
    m68k_wr16(HW_Z80_RESET, 0x0100);
}

static void vdp_init(void)
{
    (void)m68k_rd16(HW_VDP_CTRL);    /* reset the command latch */
    vdp_set_reg(0, 0x14);            /* HINT on, H/V counter latch off */
    vdp_set_reg(1, 0x14);            /* display off, VINT off, DMA on, V28 */
    vdp_set_reg(2, HELLO_PLANE_A >> 10);
    vdp_set_reg(4, 0x07);            /* plane B at E000 */
    vdp_set_reg(10, 27);             /* HINT every 28 lines */
    vdp_set_reg(12, 0x81);           /* H40 */
    vdp_set_reg(15, 2);
    vdp_set_reg(16, 0x01);           /* 64x32 planes */
}

static void load_palette(void)
{
    /* DMA 16 words from ROM to CRAM 0. */
    vdp_set_reg(19, 16);
    vdp_set_reg(20, 0);
    vdp_set_reg(21, (HELLO_ADDR_PALETTE >> 1) & 0xFF);
    vdp_set_reg(22, (HELLO_ADDR_PALETTE >> 9) & 0xFF);
    vdp_set_reg(23, (HELLO_ADDR_PALETTE >> 17) & 0x7F);
    m68k_wr32(HW_VDP_CTRL, VDP_CMD(VDP_CODE_CRAM_WR | VDP_CODE_DMA, 0));
}

static void clear_plane_a(void)
{
    /* DMA fill 64x32 cells x 2 bytes with zero. */
    vdp_set_reg(15, 1);
    vdp_set_reg(19, 0xFF);
    vdp_set_reg(20, 0x0F);
    vdp_set_reg(23, 0x80);
    m68k_wr32(HW_VDP_CTRL, VDP_CMD(VDP_CODE_VRAM_WR | VDP_CODE_DMA, HELLO_PLANE_A));
    m68k_wr16(HW_VDP_DATA, 0x0000);
    vdp_set_reg(15, 2);
}

static void print_message(void)
{
    m68k_wr32(HW_VDP_CTRL, VDP_VRAM_WRITE(HELLO_TEXT_VRAM));
    for (uint32_t p = HELLO_ADDR_MESSAGE;; p++) {
        uint8_t c = m68k_rd8(p);
        if (c == 0) break;
        m68k_wr16(HW_VDP_DATA, (uint16_t)(c - 0x20)); /* tile = ASCII - 32 */
    }
}

static void hello_main(void)
{
    m68k_clr16(HELLO_RAM_FRAMES);
    m68k_clr8(HELLO_RAM_VFLAG);
    m68k_clr8(HELLO_RAM_PAD);
    m68k_clr16(HELLO_RAM_HINTS);
    m68k_wr8(HW_IO_CTRL1, 0x40);     /* TH as output */

    z80_init();
    vdp_init();
    load_palette();
    clear_plane_a();
    print_message();

    vdp_set_reg(1, 0x74);            /* display on, VINT on, DMA on */
    M68K.sr = 0x2000;                /* move #$2000,sr */

    for (;;) {
        m68k_wr8(HELLO_RAM_VFLAG, 1);
        wait_until(vflag_clear, NULL); /* original: tst.b; bne.s loop */
    }
}

static void hello_hint(void)
{
    M68K_RMW16(HELLO_RAM_HINTS, v + 1);
}

static uint8_t read_pad(void)
{
    m68k_wr8(HW_IO_DATA1, 0x40);
    uint8_t hi = m68k_rd8(HW_IO_DATA1);          /* ?1CBRLDU */
    m68k_wr8(HW_IO_DATA1, 0x00);
    uint8_t lo = m68k_rd8(HW_IO_DATA1);          /* ?0SA00DU */
    uint8_t v = (uint8_t)((hi & 0x3F) | ((lo << 2) & 0xC0));
    return (uint8_t)~v;                          /* SACBRLDU, active high */
}

static void hello_vint(void)
{
    M68K_RMW16(HELLO_RAM_FRAMES, v + 1);
    m68k_wr8(HELLO_RAM_PAD, read_pad());
    m68k_clr8(HELLO_RAM_VFLAG);
}

static void hello_error(void)
{
    nmd_fatal("hello: error trap");
}
