/* host_null.c - headless host with optional scripted input. */
#include "nativemd/host.h"

static void null_present(void *user, uint64_t frame, const uint32_t *argb, int w, int h)
{
    (void)frame; (void)argb; (void)w; (void)h;
    ((nmd_null_host *)user)->frames_presented++;
}

static uint16_t null_poll(void *user, int port, uint64_t frame)
{
    nmd_null_host *s = user;
    if (port < 0 || port > 1 || !s->script[port] || s->script_len[port] == 0) return 0;
    size_t i = frame < s->script_len[port] ? (size_t)frame : s->script_len[port] - 1;
    return s->script[port][i];
}

nmd_host nmd_null_host_make(nmd_null_host *state)
{
    nmd_host h = {0};
    h.user = state;
    h.present_frame = null_present;
    h.poll_pad = null_poll;
    return h;
}
