/*
 * host.h - The small interface between the engine and the platform.
 *
 * Everything platform-specific (time, files, window, input, audio) goes
 * through this table. The engine ships a null (headless) host; a desktop host
 * comes later. Every callback may be NULL.
 */
#ifndef NATIVEMD_HOST_H
#define NATIVEMD_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pad buttons, active high (the engine inverts them for the I/O ports). */
enum {
    NMD_PAD_UP    = 1 << 0,
    NMD_PAD_DOWN  = 1 << 1,
    NMD_PAD_LEFT  = 1 << 2,
    NMD_PAD_RIGHT = 1 << 3,
    NMD_PAD_B     = 1 << 4,
    NMD_PAD_C     = 1 << 5,
    NMD_PAD_A     = 1 << 6,
    NMD_PAD_START = 1 << 7,
    NMD_PAD_Z     = 1 << 8,
    NMD_PAD_Y     = 1 << 9,
    NMD_PAD_X     = 1 << 10,
    NMD_PAD_MODE  = 1 << 11,
};

typedef struct nmd_host {
    void *user;

    uint64_t (*time_us)(void *user);

    /* Load/store battery saves. Return bytes transferred, or -1. */
    long (*load_save)(void *user, const char *name, void *buf, size_t size);
    long (*store_save)(void *user, const char *name, const void *buf, size_t size);

    /* Called once per frame with the frame number (0-based). */
    void (*present_frame)(void *user, uint64_t frame, const uint32_t *argb,
                          int width, int height);

    /* Interleaved stereo int16 samples. */
    void (*push_audio)(void *user, const int16_t *samples, size_t frames);

    /* Button state (NMD_PAD_*) for a port (0 or 1), sampled once per frame. */
    uint16_t (*poll_pad)(void *user, int port, uint64_t frame);
} nmd_host;

/* Headless host: no window, no audio; optional scripted input. */
typedef struct nmd_null_host {
    const uint16_t *script[2]; /* per-port button state per frame, or NULL */
    size_t          script_len[2];
    uint64_t        frames_presented;
} nmd_null_host;

nmd_host nmd_null_host_make(nmd_null_host *state);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEMD_HOST_H */
