/* png.h - write a framebuffer as a PNG file (no dependencies: stored deflate blocks). */
#ifndef NMD_HARNESS_PNG_H
#define NMD_HARNESS_PNG_H

#include <stdbool.h>
#include <stdint.h>

/* argb: rows of `stride` pixels, 0xAARRGGBB (alpha ignored). True on success. */
bool nmd_png_write(const char *path, const uint32_t *argb, int width, int height, int stride);

#endif
