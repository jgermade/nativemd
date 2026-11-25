/*
 * hw.h - Names for Mega Drive hardware addresses (68000 view).
 */
#ifndef NATIVEMD_HW_H
#define NATIVEMD_HW_H

/* Memory regions */
#define HW_ROM_START        0x000000u
#define HW_ROM_END          0x3FFFFFu
#define HW_Z80_START        0xA00000u
#define HW_Z80_RAM_END      0xA01FFFu
#define HW_Z80_END          0xA0FFFFu
#define HW_RAM_START        0xFF0000u /* 64 KB, mirrored from E00000 */
#define HW_RAM_END          0xFFFFFFu

/* Z80 area (68000 view, requires the Z80 bus) */
#define HW_YM2612_A0        0xA04000u
#define HW_YM2612_D0        0xA04001u
#define HW_YM2612_A1        0xA04002u
#define HW_YM2612_D1        0xA04003u
#define HW_Z80_BANK         0xA06000u

/* I/O */
#define HW_IO_VERSION       0xA10001u
#define HW_IO_DATA1         0xA10003u
#define HW_IO_DATA2         0xA10005u
#define HW_IO_DATA3         0xA10007u
#define HW_IO_CTRL1         0xA10009u
#define HW_IO_CTRL2         0xA1000Bu
#define HW_IO_CTRL3         0xA1000Du

/* System control */
#define HW_Z80_BUSREQ       0xA11100u
#define HW_Z80_RESET        0xA11200u
#define HW_SRAM_CTRL        0xA130F1u
#define HW_TMSS             0xA14000u

/* VDP */
#define HW_VDP_DATA         0xC00000u
#define HW_VDP_CTRL         0xC00004u
#define HW_VDP_HVCOUNTER    0xC00008u
#define HW_PSG              0xC00011u

/* VDP register write: control-port word 0x8000 | reg << 8 | value */
#define VDP_REG(r, v)       ((uint16_t)(0x8000u | ((unsigned)(r) << 8) | ((v) & 0xFFu)))

/* VDP access commands (first and second control-port words, as a long) */
#define VDP_CMD(code, addr) ((uint32_t)((((code) & 3u) << 30) | (((addr) & 0x3FFFu) << 16) | \
                                        (((code) & 0x3Cu) << 2) | (((addr) >> 14) & 3u)))
#define VDP_CODE_VRAM_RD    0x00u
#define VDP_CODE_VRAM_WR    0x01u
#define VDP_CODE_CRAM_WR    0x03u
#define VDP_CODE_VSRAM_RD   0x04u
#define VDP_CODE_VSRAM_WR   0x05u
#define VDP_CODE_CRAM_RD    0x08u
#define VDP_CODE_DMA        0x20u

#define VDP_VRAM_WRITE(a)   VDP_CMD(VDP_CODE_VRAM_WR, (a))
#define VDP_CRAM_WRITE(a)   VDP_CMD(VDP_CODE_CRAM_WR, (a))
#define VDP_VSRAM_WRITE(a)  VDP_CMD(VDP_CODE_VSRAM_WR, (a))

/* VDP status bits */
#define VDP_STATUS_PAL      0x0001u
#define VDP_STATUS_DMA      0x0002u
#define VDP_STATUS_HBLANK   0x0004u
#define VDP_STATUS_VBLANK   0x0008u
#define VDP_STATUS_ODD      0x0010u
#define VDP_STATUS_SCOL     0x0020u
#define VDP_STATUS_SOVR     0x0040u
#define VDP_STATUS_VINT     0x0080u
#define VDP_STATUS_FULL     0x0100u
#define VDP_STATUS_EMPTY    0x0200u

/* Exception vectors (byte offsets in ROM) */
#define HW_VEC_SSP          0x000u
#define HW_VEC_RESET        0x004u
#define HW_VEC_HINT         0x070u /* level 4 autovector */
#define HW_VEC_VINT         0x078u /* level 6 autovector */

/* Cartridge header */
#define HW_HDR_SYSTEM       0x100u
#define HW_HDR_COPYRIGHT    0x110u
#define HW_HDR_NAME_DOM     0x120u
#define HW_HDR_NAME_INTL    0x150u
#define HW_HDR_SERIAL       0x180u
#define HW_HDR_CHECKSUM     0x18Eu
#define HW_HDR_IO           0x190u
#define HW_HDR_ROM_RANGE    0x1A0u
#define HW_HDR_RAM_RANGE    0x1A8u
#define HW_HDR_SRAM         0x1B0u
#define HW_HDR_REGION       0x1F0u

#endif /* NATIVEMD_HW_H */
