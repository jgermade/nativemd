/*
 * render.c - the VDP's picture, one line at a time (milestone 4).
 *
 * vdp_line() calls vdp_render_line() for every active line before the line's HINT, so
 * what an HINT handler changes (palette, scroll) shows from the next line on, in the
 * engine and in the oracle alike. Mode 5 only: planes A and B (full / cell / line
 * horizontal scroll, full / two-cell vertical scroll), the window, sprites (the link
 * list, the per-line sprite and dot limits, masking by X = 0), the three priority
 * levels, the backdrop, the left-column blank. Not yet: shadow / highlight, interlace,
 * mode 4, the partial first column of two-cell vertical scroll.
 * Pixels are 0xFFRRGGBB; colours are CRAM's 3 bits per channel scaled to 8 bits.
 */
#include <string.h>

#include "internal.h"

#define V (NMD.vdp)

uint32_t nmd_fb[NMD_FB_WIDTH * NMD_FB_HEIGHT];

static uint32_t rgb(uint16_t c)
{
    static const uint8_t level[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
    return 0xFF000000u | (uint32_t)level[(c >> 1) & 7] << 16 | (uint32_t)level[(c >> 5) & 7] << 8 |
           level[(c >> 9) & 7];
}

static bool h40(void) { return (V.reg[12] & 0x81) != 0; }
unsigned vdp_width(void) { return h40() ? 320 : 256; }

/* One layer's pixel: palette index (0-63, 0 in a line = transparent) and priority. */
typedef struct { uint8_t col, pri; } px;

static uint8_t pattern_pixel(unsigned tile, unsigned row, unsigned col)
{
    uint8_t b = V.vram[(tile * 32 + row * 4 + (col >> 1)) & 0xFFFF];
    return (col & 1) ? (b & 0x0F) : (b >> 4);
}

/* The pixel of a name-table entry at (x, y) inside its cell. */
static px cell_pixel(uint16_t name, unsigned x, unsigned y)
{
    unsigned row = (name & 0x1000) ? 7 - (y & 7) : (y & 7);
    unsigned col = (name & 0x0800) ? 7 - (x & 7) : (x & 7);
    uint8_t p = pattern_pixel(name & 0x7FF, row, col);
    px r = { (uint8_t)(p ? ((name >> 13) & 3) * 16 + p : 0), (uint8_t)(name >> 15) };
    return r;
}

static const unsigned plane_cells[4] = { 32, 64, 32, 128 };   /* size code 2 is invalid */

/* Plane A (which = 0) or B (1) on this line. */
static void draw_plane(unsigned line, int which, px *out, unsigned width)
{
    unsigned base = which == 0 ? (V.reg[2] & 0x38u) << 10 : (V.reg[4] & 0x07u) << 13;
    unsigned w = plane_cells[V.reg[16] & 3], h = plane_cells[(V.reg[16] >> 4) & 3];
    unsigned hs_base = (V.reg[13] & 0x3Fu) << 10, hs_line;
    switch (V.reg[11] & 3) {
    case 0:  hs_line = 0; break;                        /* whole screen */
    case 1:  hs_line = line & 7; break;                 /* (invalid: the first 8 lines repeat) */
    case 2:  hs_line = line & ~7u; break;               /* per cell */
    default: hs_line = line; break;                     /* per line */
    }
    unsigned hs = be16(V.vram + ((hs_base + hs_line * 4 + (unsigned)which * 2) & 0xFFFF)) & 0x3FF;
    bool cols = (V.reg[11] & 4) != 0;                   /* two-cell vertical scroll */
    unsigned wmask = w * 8 - 1, hmask = h * 8 - 1, vs = V.vsram[which] & 0x3FF;
    for (unsigned x = 0; x < width;) {
        if (cols) vs = V.vsram[((x >> 4) * 2 + (unsigned)which) % 40] & 0x3FF;
        unsigned pxl = (x - hs) & wmask, pyl = (line + vs) & hmask;
        uint16_t name = be16(V.vram + ((base + ((pyl >> 3) * w + (pxl >> 3)) * 2) & 0xFFFF));
        unsigned row = (name & 0x1000) ? 7 - (pyl & 7) : (pyl & 7);
        const uint8_t *pat = V.vram + (((name & 0x7FFu) * 32 + row * 4) & 0xFFFF);
        uint8_t pal = (uint8_t)(((name >> 13) & 3) * 16), pri = (uint8_t)(name >> 15);
        /* the rest of this cell (and of a two-cell column) */
        unsigned n = 8 - (pxl & 7);
        if (cols && ((x & ~15u) + 16 - x) < n) n = (x & ~15u) + 16 - x;
        for (unsigned k = 0; k < n && x < width; k++, x++) {
            unsigned c = (pxl + k) & 7;
            if (name & 0x0800) c = 7 - c;
            uint8_t b = pat[c >> 1], p = (c & 1) ? (b & 0x0F) : (b >> 4);
            out[x].col = p ? (uint8_t)(pal + p) : 0;
            out[x].pri = pri;
        }
    }
}

/* The window replaces plane A where it is active (fixed, no scrolling). */
static void draw_window(unsigned line, px *a, unsigned width)
{
    bool wide = h40();
    unsigned base = (V.reg[3] & (wide ? 0x3Cu : 0x3Eu)) << 10, w = wide ? 64 : 32;
    unsigned hp = (V.reg[17] & 0x1Fu) * 16, vp = (V.reg[18] & 0x1Fu) * 8;
    bool right = (V.reg[17] & 0x80) != 0, down = (V.reg[18] & 0x80) != 0;
    bool row_in = down ? line >= vp : line < vp;
    for (unsigned x = 0; x < width; x++) {
        bool col_in = right ? x >= hp : x < hp;
        if (!row_in && !col_in) continue;
        uint16_t name = be16(V.vram + ((base + ((line >> 3) * w + (x >> 3)) * 2) & 0xFFFF));
        a[x] = cell_pixel(name, x, line);
    }
}

static void draw_sprites(unsigned line, px *out, unsigned width)
{
    bool wide = h40();
    unsigned sat = (V.reg[5] & (wide ? 0x7Eu : 0x7Fu)) << 9;
    unsigned max_total = wide ? 80 : 64, max_line = wide ? 20 : 16, max_dots = width;
    unsigned index = 0, on_line = 0, dots = 0;
    bool nonzero_x = false, masked = false;
    memset(out, 0, width * sizeof *out);
    for (unsigned n = 0; n < max_total; n++) {
        const uint8_t *e = V.vram + ((sat + index * 8) & 0xFFFF);
        int y = (int)(be16(e) & 0x3FF) - 128;
        unsigned hcells = ((e[2] >> 2) & 3) + 1, vcells = (e[2] & 3) + 1;
        unsigned link = e[3] & 0x7F;
        uint16_t name = be16(e + 4);
        unsigned xraw = be16(e + 6) & 0x1FF;
        if ((int)line >= y && (int)line < y + (int)vcells * 8) {
            if (++on_line > max_line) break;            /* sprite overflow */
            if (xraw == 0 && nonzero_x) masked = true;  /* X = 0 masks the rest of the line */
            if (xraw != 0) nonzero_x = true;
            unsigned sy = line - (unsigned)y;
            unsigned row = (name & 0x1000) ? vcells * 8 - 1 - sy : sy;
            for (unsigned sx = 0; sx < hcells * 8; sx++) {
                if (dots >= max_dots) break;            /* dot overflow */
                dots++;
                if (masked) continue;
                int x = (int)xraw - 128 + (int)sx;
                if (x < 0 || x >= (int)width || out[x].col) continue;
                unsigned col = (name & 0x0800) ? hcells * 8 - 1 - sx : sx;
                unsigned tile = (name & 0x7FF) + (col >> 3) * vcells + (row >> 3);
                uint8_t p = pattern_pixel(tile, row & 7, col & 7);
                if (p) {
                    out[x].col = (uint8_t)(((name >> 13) & 3) * 16 + p);
                    out[x].pri = (uint8_t)(name >> 15);
                }
            }
            if (dots >= max_dots) break;
        }
        if (link == 0 || link >= max_total) break;
        index = link;
    }
}

void vdp_render_line(unsigned line)
{
    if (line >= NMD_FB_HEIGHT || NMD.cfg.no_render) return;
    uint32_t *row = nmd_fb + line * NMD_FB_WIDTH;
    unsigned width = vdp_width();
    uint32_t backdrop = rgb(V.cram[V.reg[7] & 0x3F]);
    if (!(V.reg[1] & 0x04)) {                           /* mode 4: not rendered */
        for (unsigned x = 0; x < NMD_FB_WIDTH; x++) row[x] = 0xFF000000u;
        return;
    }
    if (!(V.reg[1] & 0x40)) {                           /* display off: the backdrop */
        for (unsigned x = 0; x < NMD_FB_WIDTH; x++) row[x] = backdrop;
        return;
    }
    px a[320], b[320], s[320];
    draw_plane(line, 1, b, width);
    draw_plane(line, 0, a, width);
    draw_window(line, a, width);
    draw_sprites(line, s, width);
    for (unsigned x = 0; x < width; x++) {
        const px *top;
        if      (s[x].col && s[x].pri) top = &s[x];
        else if (a[x].col && a[x].pri) top = &a[x];
        else if (b[x].col && b[x].pri) top = &b[x];
        else if (s[x].col)             top = &s[x];
        else if (a[x].col)             top = &a[x];
        else if (b[x].col)             top = &b[x];
        else                           top = NULL;
        row[x] = top ? rgb(V.cram[top->col]) : backdrop;
    }
    if (V.reg[0] & 0x20)                                /* left column blank */
        for (unsigned x = 0; x < 8; x++) row[x] = backdrop;
    for (unsigned x = width; x < NMD_FB_WIDTH; x++) row[x] = 0xFF000000u;
}
