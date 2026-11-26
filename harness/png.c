/*
 * png.c - a minimal PNG writer for screenshots: 8-bit RGB, no filtering, the image data
 * in stored (uncompressed) deflate blocks, so it needs neither zlib nor a compressor.
 * Files are larger than a real encoder's (about 3 bytes per pixel), which is fine for
 * screenshots of 320x240.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"

static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static bool chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)n);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
    crc = crc32_update(crc, data, n) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4;
}

bool nmd_png_write(const char *path, const uint32_t *argb, int width, int height, int stride)
{
    size_t row = (size_t)width * 3 + 1, raw_n = row * (size_t)height;
    uint8_t *raw = malloc(raw_n);
    size_t blocks = raw_n / 65535 + 1, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n);
    if (!raw || !z) { free(raw); free(z); return false; }
    for (int y = 0; y < height; y++) {
        uint8_t *r = raw + (size_t)y * row;
        r[0] = 0;                                       /* filter: none */
        for (int x = 0; x < width; x++) {
            uint32_t p = argb[(size_t)y * (size_t)stride + (size_t)x];
            r[1 + x * 3] = (uint8_t)(p >> 16);
            r[2 + x * 3] = (uint8_t)(p >> 8);
            r[3 + x * 3] = (uint8_t)p;
        }
    }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;                       /* zlib header, no compression */
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw_n; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
    for (size_t done = 0; done < raw_n || done == 0;) {
        size_t n = raw_n - done > 65535 ? 65535 : raw_n - done;
        z[o++] = done + n == raw_n ? 1 : 0;             /* BFINAL, BTYPE = stored */
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + done, n);
        o += n;
        done += n;
        if (raw_n == 0) break;
    }
    put32(z + o, s2 << 16 | s1);
    o += 4;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)width);
    put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* 8-bit RGB */
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, sizeof ihdr) &&
              chunk(f, "IDAT", z, o) && chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = false;
    free(raw);
    free(z);
    return ok;
}
