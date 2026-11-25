/*
 * machine.c - Machine lifetime, frame scheduler, interrupts, fatal errors.
 *
 * Scheduling model (docs/contract.md, "Execution model"):
 *   - The game's reset routine runs in a coroutine on the single host thread.
 *   - It runs until it reaches a wait point (wait_vblank / wait_until).
 *   - The engine then runs the active display (line loop, HINT), raises
 *     VBlank, runs the VINT handler, services chips and the host, and resumes
 *     the game. Interrupts are therefore only taken at wait points.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "coro.h"

nmd_machine NMD;

static nmd_coro *game_coro;
static nmd_fatal_handler fatal_handler;
static uint32_t framebuffer[320 * 240];

#define GAME_STACK_SIZE (512u * 1024u)

/* ------------------------------------------------------------------------ */
/* Fatal errors                                                              */
/* ------------------------------------------------------------------------ */

void nmd_set_fatal_handler(nmd_fatal_handler handler) { fatal_handler = handler; }

void nmd_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (nmd_trace_enabled()) nmd_trace_comment("fatal %s", msg);
    if (fatal_handler) fatal_handler(msg);
    fprintf(stderr, "nativemd: fatal: %s (frame %llu)\n", msg, (unsigned long long)NMD.frame);
    abort();
}

/* ------------------------------------------------------------------------ */
/* Lifetime                                                                  */
/* ------------------------------------------------------------------------ */

bool nmd_init(const nmd_config *cfg, const nmd_game *game, const uint8_t *rom, size_t rom_size)
{
    nmd_shutdown();
    if (!nmd_cart_parse(rom, rom_size, &NMD.cart)) {
        fprintf(stderr, "nativemd: ROM image too small (%zu bytes)\n", rom_size);
        return false;
    }
    if (rom_size > 0x400000 && game->mapper == NMD_MAPPER_NONE) {
        fprintf(stderr, "nativemd: ROM larger than 4 MB needs a mapper\n");
        return false;
    }
    if (game->chip_count > NMD_MAX_CHIPS) {
        fprintf(stderr, "nativemd: too many chips (%zu > %d)\n", game->chip_count, NMD_MAX_CHIPS);
        return false;
    }
    NMD.rom = malloc(rom_size);
    if (!NMD.rom) return false;
    memcpy(NMD.rom, rom, rom_size);
    NMD.rom_size = rom_size;
    NMD.cfg = *cfg;
    nmd_rom_writes_clear();
    NMD.game = game;
    for (size_t i = 0; i < game->chip_count; i++) NMD.chips[i] = game->chips[i];
    NMD.chip_count = game->chip_count;

    if (NMD.cart.checksum_header != NMD.cart.checksum_actual)
        fprintf(stderr, "nativemd: warning: header checksum %04X, computed %04X\n",
                NMD.cart.checksum_header, NMD.cart.checksum_actual);

    vdp_reset();
    io_reset();
    z80_reset_state();
    for (size_t i = 0; i < NMD.chip_count; i++)
        if (NMD.chips[i].reset) NMD.chips[i].reset(NMD.chips[i].user);
    return true;
}

void nmd_shutdown(void)
{
    coro_destroy(game_coro);
    game_coro = NULL;
    free(NMD.rom);
    memset(&NMD, 0, sizeof NMD);
    memset(&M68K, 0, sizeof M68K);
}

const nmd_cart_info *nmd_cart(void) { return &NMD.cart; }
uint64_t nmd_frame_count(void)      { return NMD.frame; }
bool nmd_game_finished(void)        { return NMD.finished; }

/* ------------------------------------------------------------------------ */
/* Interrupts                                                                */
/* ------------------------------------------------------------------------ */

/* Returns false if the interrupt is masked (it then stays pending). */
static bool take_interrupt(unsigned level, uint32_t vector)
{
    if (((M68K.sr & M68K_SR_IMASK) >> 8) >= level) return false;
    /* The acknowledge cycle clears the VDP's VINT flag before the handler runs. */
    if (level == 6) vdp_vint_ack();
    uint32_t handler = be32(NMD.rom + vector) & 0xFFFFFF;
    nmd_routine_fn fn = find_routine(handler);

    m68k_regs saved = M68K;
    M68K.sr = (uint16_t)((M68K.sr & ~M68K_SR_IMASK) | (level << 8) | M68K_SR_S);
    if (nmd_trace_enabled()) nmd_trace_comment("irq %u %06X", level, handler);
    if (fn) nmd_run_c(handler, fn);
    else if (!nmd_run_missing_routine(handler, true))
        nmd_fatal("interrupt level %u: no C for handler %06X (vector %03X)", level, handler, vector);
    if (nmd_trace_enabled()) nmd_trace_comment("rte %u", level);
    M68K = saved;
    return true;
}

static void hint_dispatch(void) { (void)take_interrupt(4, HW_VEC_HINT); }

/* ------------------------------------------------------------------------ */
/* Scheduler                                                                 */
/* ------------------------------------------------------------------------ */

static void game_entry(void)
{
    uint32_t pc = be32(NMD.rom + HW_VEC_RESET) & 0xFFFFFF;
    nmd_routine_fn fn = find_routine(pc);
    if (fn) nmd_run_c(pc, fn);
    else if (!nmd_run_missing_routine(pc, false)) nmd_fatal("reset: no C for reset PC %06X", pc);
}

static void start_game(void)
{
    memset(&M68K, 0, sizeof M68K);
    M68K.a[7] = be32(NMD.rom + HW_VEC_SSP);
    M68K.sr = 0x2700;
    game_coro = coro_create(game_entry, GAME_STACK_SIZE);
    if (!game_coro) nmd_fatal("out of memory creating the game coroutine");
    NMD.started = true;
}

static void sample_pads(void)
{
    const nmd_host *h = &NMD.cfg.host;
    for (int p = 0; p < 2; p++)
        NMD.io.pad[p] = h->poll_pad ? h->poll_pad(h->user, p, NMD.frame) : 0;
}

static void run_one_frame(void)
{
    /* Game code until its next wait point. */
    coro_resume(game_coro);
    if (coro_done(game_coro)) {
        NMD.finished = true;
        return;
    }

    /* Active display, then VBlank. */
    vdp_frame(hint_dispatch);
    /*
     * A masked VINT stays pending (status bit F stays set) and is taken at the
     * next frame's VBlank if unmasked then. The hardware would take it as soon
     * as the mask is lowered; that is timing the contract does not model.
     */
    if (vdp_vint_enabled()) (void)take_interrupt(6, HW_VEC_VINT);
    else vdp_vint_ack();
    for (size_t i = 0; i < NMD.chip_count; i++)
        if (NMD.chips[i].frame) NMD.chips[i].frame(NMD.chips[i].user);

    const nmd_host *h = &NMD.cfg.host;
    if (h->present_frame) {
        /* TODO(milestone 4): rendered image. Until then the buffer is black. */
        int lines = (NMD.vdp.reg[1] & 0x08) ? 240 : 224;
        int width = (NMD.vdp.reg[12] & 0x81) ? 320 : 256;
        h->present_frame(h->user, NMD.frame, framebuffer, width, lines);
    }
    NMD.frame++;
    sample_pads();
}

uint64_t nmd_run_frames(uint64_t frames)
{
    if (!NMD.rom) nmd_fatal("nmd_run_frames called before nmd_init");
    if (!NMD.started) {
        start_game();
        sample_pads();
    }
    uint64_t target = NMD.frame + frames;
    while (NMD.frame < target && !NMD.finished)
        run_one_frame();
    return NMD.frame;
}

/* ------------------------------------------------------------------------ */
/* Waits (contract machine.h)                                                */
/* ------------------------------------------------------------------------ */

void wait_vblank(void)
{
    if (!game_coro) nmd_fatal("wait_vblank called outside the game coroutine");
    coro_yield(game_coro);
}

void wait_until(bool (*cond)(void *user), void *user)
{
    /*
     * The condition's reads stand for the polling loop of the original;
     * repeated identical reads are collapsed in the trace, as the oracle
     * does for the declared idle loop.
     */
    nmd_trace_poll_begin();
    while (!cond(user)) {
        nmd_trace_poll_pause();
        wait_vblank();
        nmd_trace_poll_resume();
    }
    nmd_trace_poll_end();
}

/* ------------------------------------------------------------------------ */
/* Memory views                                                              */
/* ------------------------------------------------------------------------ */

uint8_t  *nmd_ram(void)       { return NMD.ram; }
uint8_t  *nmd_vram(void)      { return NMD.vdp.vram; }
uint16_t *nmd_cram(void)      { return NMD.vdp.cram; }
uint16_t *nmd_vsram(void)     { return NMD.vdp.vsram; }
uint8_t  *nmd_vdp_regs(void)  { return NMD.vdp.reg; }
uint8_t  *nmd_z80_ram(void)   { return NMD.z80.ram; }

const uint8_t *nmd_rom(size_t *size)
{
    if (size) *size = NMD.rom_size;
    return NMD.rom;
}
