/*
 * nmd-oracle - runs the ORIGINAL 68000 code of a ROM on Musashi, with the
 * nativemd hardware model behind it, and records the bus trace that the
 * game's C must reproduce (docs/harness.md).
 *
 * The oracle checks the CPU side: same ROM, same hardware model, real 68000
 * semantics. The hardware model itself is validated separately (test ROMs,
 * other emulators).
 *
 * Modes:
 *   frames:  nmd-oracle ROM --frames N [options]
 *            Reset, run with line timing (NTSC, 262 lines of 3420 master
 *            clocks), HINT/VINT as the VDP raises them; stop right after the
 *            RTE of the N-th VINT handler.
 *   routine: nmd-oracle ROM --state IN.nmds --call ADDR [options]
 *            Load a state, push a sentinel return address, call ADDR and stop
 *            when it returns. No interrupts.
 *
 * Options:
 *   --trace FILE        write the bus trace
 *   --state-out FILE    write the final state
 *   --input FILE        pad script: one line per frame, "P1 [P2]" in hex (NMD_PAD_*)
 *   --sym FILE          symbols "ADDRESS NAME" (for --idle)
 *   --idle START[:END]  idle loop by label or hex address (END defaults to the
 *                       next symbol): repeated identical reads are collapsed
 *   --stack LO-HI       hex range of stack accesses to drop (default: the
 *                       1 KB below the initial SSP)
 *   --coverage FILE     write the addresses of executed instructions
 *   --irq               routine mode: ADDR is an interrupt handler (ends in
 *                       RTE), so push an exception frame instead of a return address
 *   --max-cycles N      routine mode safety limit (default 10000000)
 *
 * Corrections to Musashi (see docs/harness.md, "Oracle"):
 *   - CLR to memory: the 68000 reads the operand before writing; Musashi
 *     omits that read, so the oracle performs and traces it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "../../src/internal.h"

/* Declared in m68kcpu.c but not in m68k.h. */
void m68k_set_rte_instr_callback(void (*callback)(void));

#define SENTINEL 0x00BEEF00u
#define MCLK_PER_LINE 3420u
#define LINES_NTSC 262u

static FILE    *trace;
static uint32_t stack_lo, stack_hi;            /* dropped accesses: [lo, hi] */
static uint32_t idle_lo, idle_hi;              /* idle loop: [lo, hi) */
static bool     have_idle;
static bool     idle_have_last;
static nmd_trace_line idle_last;

static bool     in_exception;                  /* between IACK and first handler insn */
static int      irq_depth;
static unsigned irq_levels[8];
static bool     hint_pending, vint_pending;
static uint64_t vint_count, vint_target;
static bool     stop;
static bool     routine_mode;
static bool     clr_read_pending;              /* current insn is CLR <mem> */

static uint8_t *coverage;                      /* one byte per even ROM address */
static size_t   coverage_size;

static uint16_t (*pad_script)[2];
static size_t   pad_frames;

/* ------------------------------------------------------------------------ */
/* Tracing with the oracle's filters                                        */
/* ------------------------------------------------------------------------ */

static bool in_idle_loop(void)
{
    if (!have_idle || irq_depth > 0) return false;
    uint32_t ppc = m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF;
    return ppc >= idle_lo && ppc < idle_hi;
}

static void emit(bool write, unsigned bits, uint32_t addr, uint32_t value)
{
    if (!trace || in_exception) return;
    addr &= 0xFFFFFF;
    if (addr >= stack_lo && addr <= stack_hi) return;
    if (!write && in_idle_loop()) {
        if (idle_have_last && idle_last.bits == bits && idle_last.addr == addr && idle_last.value == value)
            return;
        idle_last.bits = bits;
        idle_last.addr = addr;
        idle_last.value = value;
        idle_have_last = true;
    }
    nmd_trace_access(NMD_TRACE_M68K, write, bits, addr, value);
}

/* ------------------------------------------------------------------------ */
/* Musashi memory callbacks                                                  */
/* ------------------------------------------------------------------------ */

unsigned int m68k_read_memory_8(unsigned int a)
{
    uint8_t v = nmd_bus_rd8_raw(a);
    emit(false, 8, a, v);
    return v;
}

unsigned int m68k_read_memory_16(unsigned int a)
{
    uint16_t v = nmd_bus_rd16_raw(a);
    emit(false, 16, a, v);
    return v;
}

unsigned int m68k_read_memory_32(unsigned int a)
{
    unsigned hi = m68k_read_memory_16(a);
    unsigned lo = m68k_read_memory_16(a + 2);
    return hi << 16 | lo;
}

/* PC-relative reads are data reads (tables addressed relative to PC). */
unsigned int m68k_read_pcrelative_8(unsigned int a)  { return m68k_read_memory_8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return m68k_read_memory_32(a); }

/*
 * Instruction fetches: not traced. Musashi fetches the opcode after the
 * instruction hook has asked it to stop, so the sentinel reads as NOP.
 */
unsigned int m68k_read_immediate_16(unsigned int a)
{
    if (routine_mode && (a & 0xFFFFFF) == SENTINEL) return 0x4E71;
    return nmd_bus_rd16_raw(a);
}
unsigned int m68k_read_immediate_32(unsigned int a)
{
    return (unsigned)nmd_bus_rd16_raw(a) << 16 | nmd_bus_rd16_raw(a + 2);
}

unsigned int m68k_read_disassembler_8(unsigned int a)  { return nmd_bus_rd8_raw(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return nmd_bus_rd16_raw(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_immediate_32(a); }

/* CLR's read cycle (absent in Musashi), performed before its first write. */
static void clr_read(unsigned int a, unsigned bits)
{
    if (!clr_read_pending) return;
    clr_read_pending = false;
    if (bits == 8) (void)m68k_read_memory_8(a);
    else if (bits == 16) (void)m68k_read_memory_16(a);
    else (void)m68k_read_memory_32(a);
}

void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    clr_read(a, 8);
    emit(true, 8, a, v & 0xFF);
    nmd_bus_wr8_raw(a, (uint8_t)v);
}

void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    clr_read(a, 16);
    emit(true, 16, a, v & 0xFFFF);
    nmd_bus_wr16_raw(a, (uint16_t)v);
}

/* High word first (contract: to verify per addressing mode). */
void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    clr_read(a, 32);
    m68k_write_memory_16(a, v >> 16);
    m68k_write_memory_16(a + 2, v & 0xFFFF);
}

/* MOVE.L to -(An): low word first. */
void m68k_write_memory_32_pd(unsigned int a, unsigned int v)
{
    m68k_write_memory_16(a + 2, v & 0xFFFF);
    m68k_write_memory_16(a, v >> 16);
}

/* ------------------------------------------------------------------------ */
/* CPU callbacks                                                             */
/* ------------------------------------------------------------------------ */

static void update_irq(void)
{
    m68k_set_irq(vint_pending ? 6 : hint_pending ? 4 : 0);
}

static int on_int_ack(int level)
{
    if (level == 6) { vint_pending = false; vdp_vint_ack(); vint_count++; }
    if (level == 4) hint_pending = false;
    update_irq();
    in_exception = true; /* drop the vector fetch; stack pushes are dropped by range */
    if (irq_depth < 8) irq_levels[irq_depth] = (unsigned)level;
    irq_depth++;
    if (trace) nmd_trace_comment("irq %d", level);
    return M68K_INT_ACK_AUTOVECTOR;
}

static void on_rte(void)
{
    if (irq_depth <= 0) return;
    irq_depth--;
    unsigned level = irq_depth < 8 ? irq_levels[irq_depth] : 0;
    if (trace) nmd_trace_comment("rte %u", level);
    if (level == 6 && vint_target && vint_count >= vint_target) {
        stop = true;
        m68k_end_timeslice();
    }
}

static int on_tas(void) { return 0; } /* Mega Drive: TAS write-back does not complete */

static void on_instr(unsigned int pc)
{
    in_exception = false;
    pc &= 0xFFFFFF;
    if (routine_mode && pc == SENTINEL) {
        stop = true;
        m68k_end_timeslice();
        return;
    }
    /* CLR.b/w/l <ea> with a memory operand: 0100 0010 ss mmm rrr, ss != 11, mmm != 000 */
    uint16_t op = nmd_bus_rd16_raw(pc & ~1u);
    clr_read_pending = (op & 0xFF00) == 0x4200 && (op & 0x00C0) != 0x00C0 && (op & 0x0038) != 0;
    if (have_idle && irq_depth == 0 && !(pc >= idle_lo && pc < idle_hi)) idle_have_last = false;
    if (coverage && pc < coverage_size * 2) coverage[pc >> 1] = 1;
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void fatal_handler(const char *msg)
{
    fprintf(stderr, "nmd-oracle: fatal: %s (PC %06X)\n", msg, m68k_get_reg(NULL, M68K_REG_PPC));
    exit(3);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f);
    buf[n] = 0;
    *size = (size_t)n;
    return buf;
}

typedef struct sym { uint32_t addr; char name[64]; } sym;
static sym   *syms;
static size_t nsyms;

static void load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[128];
    while (fgets(line, sizeof line, f)) {
        sym s;
        if (sscanf(line, "%x %63s", &s.addr, s.name) != 2) continue;
        syms = realloc(syms, (nsyms + 1) * sizeof *syms);
        syms[nsyms++] = s;
    }
    fclose(f);
}

static uint32_t resolve(const char *text, bool *found_next, uint32_t *next)
{
    for (size_t i = 0; i < nsyms; i++) {
        if (strcmp(syms[i].name, text) == 0) {
            if (found_next) {
                *found_next = false;
                uint32_t best = 0xFFFFFFFF;
                for (size_t j = 0; j < nsyms; j++)
                    if (syms[j].addr > syms[i].addr && syms[j].addr < best) { best = syms[j].addr; *found_next = true; }
                *next = best;
            }
            return syms[i].addr;
        }
    }
    char *end;
    unsigned long v = strtoul(text, &end, 16);
    if (*text == 0 || *end != 0) { fprintf(stderr, "nmd-oracle: unknown symbol or address '%s'\n", text); exit(2); }
    if (found_next) *found_next = false;
    return (uint32_t)v;
}

static void load_input(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        unsigned p1 = 0, p2 = 0;
        if (sscanf(line, "%x %x", &p1, &p2) < 1) continue;
        pad_script = realloc(pad_script, (pad_frames + 1) * sizeof *pad_script);
        pad_script[pad_frames][0] = (uint16_t)p1;
        pad_script[pad_frames][1] = (uint16_t)p2;
        pad_frames++;
    }
    fclose(f);
}

static void sample_pads(uint64_t frame)
{
    for (int p = 0; p < 2; p++) {
        uint16_t v = 0;
        if (pad_frames) v = pad_script[frame < pad_frames ? frame : pad_frames - 1][p];
        NMD.io.pad[p] = v;
    }
}

static void regs_to_engine(void)
{
    for (int i = 0; i < 8; i++) {
        M68K.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        M68K.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    M68K.sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
}

static void regs_from_engine(void)
{
    m68k_set_reg(M68K_REG_SR, M68K.sr);
    for (int i = 0; i < 8; i++) {
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), M68K.d[i]);
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), M68K.a[i]);
    }
}

/* ------------------------------------------------------------------------ */
/* Runs                                                                      */
/* ------------------------------------------------------------------------ */

static void run_frames(uint64_t frames)
{
    vint_target = frames;
    m68k_pulse_reset();                /* reads SSP/PC: before tracing starts */
    nmd_trace_open(trace);
    sample_pads(0);
    uint64_t mclk_done = 0, cycles_done = 0;
    for (uint64_t frame = 0; !stop; frame++) {
        if (frame > frames + 2) {
            fprintf(stderr, "nmd-oracle: %llu VINTs taken after %llu frames (VINT disabled?)\n",
                    (unsigned long long)vint_count, (unsigned long long)frame);
            exit(1);
        }
        vdp_begin_frame();
        unsigned active = vdp_active_lines();
        for (unsigned line = 0; line < LINES_NTSC && !stop; line++) {
            if (line < active) {
                if (vdp_line(line)) hint_pending = true;
            } else if (line == active) {
                vdp_end_active();
                sample_pads(frame);
                if (vdp_vint_enabled()) vint_pending = true;
                else vdp_vint_ack();
            }
            update_irq();
            mclk_done += MCLK_PER_LINE;
            uint64_t target = mclk_done / 7;
            while (cycles_done < target && !stop) {
                int ran = m68k_execute((int)(target - cycles_done));
                cycles_done += (uint64_t)ran;
            }
        }
    }
}

static void run_routine(uint32_t addr, uint64_t max_cycles, bool irq)
{
    routine_mode = true;
    m68k_pulse_reset();
    regs_from_engine();
    /* Return address, or a 68000 exception frame (SR, then PC) for RTE. */
    uint32_t sp = M68K.a[7] - (irq ? 6 : 4);
    uint32_t pc_at = irq ? sp + 2 : sp;
    if (irq) nmd_bus_wr16_raw(sp, M68K.sr);
    nmd_bus_wr16_raw(pc_at, (uint16_t)(SENTINEL >> 16));
    nmd_bus_wr16_raw(pc_at + 2, (uint16_t)SENTINEL);
    m68k_set_reg(M68K_REG_A7, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    nmd_trace_open(trace);
    uint64_t done = 0;
    while (!stop && done < max_cycles) done += (uint64_t)m68k_execute(10000);
    if (!stop) { fprintf(stderr, "nmd-oracle: routine %06X did not return\n", addr); exit(1); }
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *trace_path = NULL, *state_in = NULL, *state_out = NULL;
    const char *idle = NULL, *stack = NULL, *cov_path = NULL, *call = NULL;
    uint64_t frames = 0, max_cycles = 10000000;
    bool irq = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define OPT(name) (strcmp(a, name) == 0 && v && (i++, 1))
        if      (OPT("--frames"))     frames = strtoull(v, NULL, 10);
        else if (OPT("--trace"))      trace_path = v;
        else if (OPT("--state"))      state_in = v;
        else if (OPT("--state-out"))  state_out = v;
        else if (OPT("--call"))       call = v;
        else if (OPT("--input"))      load_input(v);
        else if (OPT("--sym"))        load_syms(v);
        else if (OPT("--idle"))       idle = v;
        else if (OPT("--stack"))      stack = v;
        else if (OPT("--coverage"))   cov_path = v;
        else if (OPT("--max-cycles")) max_cycles = strtoull(v, NULL, 10);
        else if (strcmp(a, "--irq") == 0) irq = true;
        else if (a[0] != '-' && !rom_path) rom_path = a;
        else { fprintf(stderr, "nmd-oracle: bad argument '%s' (see the header of nmd_oracle.c)\n", a); return 2; }
#undef OPT
    }
    if (!rom_path || (!frames && !call) || (frames && call) || (call && !state_in)) {
        fprintf(stderr, "usage: nmd-oracle ROM --frames N [opts]\n"
                        "       nmd-oracle ROM --state IN.nmds --call ADDR [opts]\n");
        return 2;
    }

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    static const nmd_game no_game = { .name = "oracle" };
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    if (!nmd_init(&cfg, &no_game, rom, rom_size)) return 2;
    nmd_set_fatal_handler(fatal_handler);
    if (state_in && !nmd_state_load(state_in)) { fprintf(stderr, "nmd-oracle: cannot load %s\n", state_in); return 2; }

    uint32_t ssp = be32(rom);
    stack_lo = (ssp - 0x400) & 0xFFFFFF;
    stack_hi = (ssp - 1) & 0xFFFFFF;
    if (stack && sscanf(stack, "%x-%x", &stack_lo, &stack_hi) != 2) {
        fprintf(stderr, "nmd-oracle: bad --stack '%s'\n", stack);
        return 2;
    }
    if (idle) {
        char lo[64], hi[64] = "";
        if (sscanf(idle, "%63[^:]:%63s", lo, hi) < 1) { fprintf(stderr, "nmd-oracle: bad --idle\n"); return 2; }
        bool has_next;
        uint32_t next;
        idle_lo = resolve(lo, &has_next, &next);
        if (hi[0]) idle_hi = resolve(hi, NULL, NULL);
        else if (has_next) idle_hi = next;
        else { fprintf(stderr, "nmd-oracle: --idle %s needs an end\n", idle); return 2; }
        have_idle = true;
    }
    if (cov_path) {
        coverage_size = (rom_size + 1) / 2;
        coverage = calloc(coverage_size, 1);
    }
    if (trace_path && !(trace = fopen(trace_path, "w"))) { perror(trace_path); return 2; }

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(on_int_ack);
    m68k_set_rte_instr_callback(on_rte);
    m68k_set_tas_instr_callback(on_tas);
    m68k_set_instr_hook_callback(on_instr);

    if (frames) run_frames(frames);
    else        run_routine(resolve(call, NULL, NULL), max_cycles, irq);

    nmd_trace_open(NULL);
    if (trace) fclose(trace);
    if (state_out) {
        regs_to_engine();
        if (!nmd_state_save(state_out)) { perror(state_out); return 2; }
    }
    if (cov_path) {
        FILE *f = fopen(cov_path, "w");
        if (!f) { perror(cov_path); return 2; }
        for (size_t i = 0; i < coverage_size; i++)
            if (coverage[i]) fprintf(f, "%06zX\n", i * 2);
        fclose(f);
    }
    nmd_shutdown();
    free(rom);
    return 0;
}
