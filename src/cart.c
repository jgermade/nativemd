/* cart.c - ROM header parsing. */
#include <string.h>

#include "internal.h"

static void copy_field(char *dst, const uint8_t *src, size_t n)
{
    memcpy(dst, src, n);
    dst[n] = '\0';
    /* Trim trailing spaces. */
    for (size_t i = n; i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\0'); i--)
        dst[i - 1] = '\0';
}

uint16_t nmd_cart_checksum(const uint8_t *rom, size_t size)
{
    uint16_t sum = 0;
    for (size_t i = 0x200; i + 1 < size; i += 2)
        sum = (uint16_t)(sum + be16(rom + i));
    if (size > 0x200 && (size & 1))
        sum = (uint16_t)(sum + (rom[size - 1] << 8));
    return sum;
}

static unsigned parse_regions(const uint8_t *p)
{
    unsigned r = 0;
    /* Old style: letters J/U/E. New style: one hex digit (bit0 JP, bit2 US, bit3 EU). */
    for (int i = 0; i < 3; i++) {
        switch (p[i]) {
        case 'J': r |= NMD_REGION_JAPAN;  break;
        case 'U': r |= NMD_REGION_USA;    break;
        case 'E': r |= NMD_REGION_EUROPE; break;
        default: break;
        }
    }
    if (r == 0) {
        unsigned v = 0;
        char c = (char)p[0];
        if (c >= '0' && c <= '9') v = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') v = (unsigned)(c - 'A' + 10);
        if (v & 1) r |= NMD_REGION_JAPAN;
        if (v & 4) r |= NMD_REGION_USA;
        if (v & 8) r |= NMD_REGION_EUROPE;
    }
    return r;
}

bool nmd_cart_parse(const uint8_t *rom, size_t size, nmd_cart_info *out)
{
    memset(out, 0, sizeof *out);
    if (!rom || size < 0x200) return false;
    copy_field(out->system,        rom + HW_HDR_SYSTEM,    16);
    copy_field(out->copyright,     rom + HW_HDR_COPYRIGHT, 16);
    copy_field(out->name_domestic, rom + HW_HDR_NAME_DOM,  48);
    copy_field(out->name_overseas, rom + HW_HDR_NAME_INTL, 48);
    copy_field(out->serial,        rom + HW_HDR_SERIAL,    14);
    out->checksum_header = be16(rom + HW_HDR_CHECKSUM);
    out->checksum_actual = nmd_cart_checksum(rom, size);
    out->rom_start = be32(rom + HW_HDR_ROM_RANGE);
    out->rom_end   = be32(rom + HW_HDR_ROM_RANGE + 4);
    if (rom[HW_HDR_SRAM] == 'R' && rom[HW_HDR_SRAM + 1] == 'A') {
        out->has_sram   = true;
        out->sram_type  = rom[HW_HDR_SRAM + 2];
        out->sram_start = be32(rom + HW_HDR_SRAM + 4);
        out->sram_end   = be32(rom + HW_HDR_SRAM + 8);
    }
    out->regions = parse_regions(rom + HW_HDR_REGION);
    return true;
}
