/*
 * oracle.c - Musashi running the original 68000 code on the nativemd
 * hardware model: traces, frames mode, routine calls, state capture and
 * hybrid execution. See oracle.h and docs/harness.md.
 *
 * Corrections to Musashi:
 *   - CLR to memory: the 68000 reads the operand before writing; Musashi
 *     omits that read, so the oracle performs and traces it.
 *   - TAS: the write-back does not complete on the Mega Drive.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "oracle.h"
#include "../../src/internal.h"

/* Declared in m68kcpu.c but not in m68k.h. */
void m68k_set_rte_instr_callback(void (*callback)(void));

#define SENTINEL       0x00BEEF00u /* return address of oracle-run routines   */
#define TRAMPOLINE     0x00BEEF10u /* PC parked here while the engine acts    */
#define MCLK_PER_LINE  3420u
#define LINES_NTSC     262u
#define LINES_PAL      313u

/* ---- filters and trace ---------------------------------------------------- */

/*
 * Stack filter. Default: an access is a stack access when it falls between the current
 * A7 and the initial SSP (the stack in use right now), allowing for Musashi's order of
 * operations: pops raise A7 before reading (up to 4 bytes below A7), and MOVEM to -(A7)
 * writes below A7 before lowering it (the range is computed from its register mask). oracle_set_stack() replaces that
 * with a fixed range. min_sp records the lowest A7 seen, for comparisons of RAM.
 */
static uint32_t stack_top;            /* initial SSP (exclusive upper bound) */
static bool     reset_started;        /* the hybrid handler has run the reset routine */
static bool     stack_fixed;
static uint32_t stack_lo, stack_hi;
static uint32_t min_sp;
static uint32_t movem_lo;             /* MOVEM <regs>,-(A7): it writes below A7 before updating A7 */
static uint32_t idle_lo, idle_hi;
static bool     have_idle;
static bool     idle_have_last;
static nmd_trace_line idle_last;

static bool     in_exception;     /* between IACK and the handler's first insn */
static bool     muted;            /* CPU reset: vector reads are not traced    */

/* Lag frames: a VINT taken before the main code polled the idle loop. */
static bool     idle_polled;      /* main code read inside the idle loop since the last VINT */
static uint64_t lag_frames, first_lag_vint;
static int      irq_depth;        /* interrupt handlers being run             */
static unsigned irq_levels[16];
static bool     clr_read_pending;

/* ---- frames mode ------------------------------------------------------------ */

static bool     frames_mode;
static bool     hint_pending, vint_pending;
static uint64_t vint_count, vint_target;
static const uint16_t (*pad_script)[2];
static size_t   pad_frames;

/* ---- run control --------------------------------------------------------------- */

typedef enum action { ACT_NONE, ACT_RETURNED, ACT_CALL_C, ACT_IDLE } action;
static action   pending_action;
static uint32_t action_pc;
static bool     divert_to_c;      /* hybrid: jump into C when PC has C        */
static bool     idle_armed;       /* hybrid: idle loop head seen once         */
static uint64_t hybrid_max_cycles = 50000000;

/* ---- outputs ---------------------------------------------------------------------- */

static uint8_t *coverage;
static size_t   coverage_words;

#define MAX_CAPTURES 64
static struct { uint32_t addr; char name[64]; unsigned count; uint64_t hits; } captures[MAX_CAPTURES];
static size_t   ncaptures;
static char     capture_dir[512] = ".";
static unsigned capture_max = 16, capture_every = 1, captured_total;

typedef struct sym { uint32_t addr; char name[64]; } sym;
static sym     *syms;
static size_t   nsyms;

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
    if (!nmd_trace_enabled() || in_exception || muted) return;
    addr &= 0xFFFFFF;
    if (stack_fixed) {
        if (addr >= stack_lo && addr <= stack_hi) return;
    } else {
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7) & 0xFFFFFF;
        if (sp < min_sp) min_sp = sp;
        /* Pops (RTS, RTE, (A7)+ moves) raise A7 before reading: up to 4 bytes below it. */
        if (addr + 4 >= sp && addr < stack_top) return;
        if (movem_lo && addr >= movem_lo && addr < sp) return;
    }
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

static bool parked(unsigned a)
{
    a &= 0xFFFFFF;
    return a == SENTINEL || a == TRAMPOLINE;
}

static void note_idle_poll(void)
{
    if (frames_mode && in_idle_loop()) idle_polled = true;
}

unsigned int m68k_read_memory_8(unsigned int a)
{
    note_idle_poll();
    uint8_t v = nmd_bus_rd8_raw(a);
    emit(false, 8, a, v);
    return v;
}

unsigned int m68k_read_memory_16(unsigned int a)
{
    note_idle_poll();
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
 * instruction hook has asked it to stop, so parked addresses read as NOP.
 */
unsigned int m68k_read_immediate_16(unsigned int a)
{
    if (parked(a)) return 0x4E71;
    return nmd_bus_rd16_raw(a);
}

unsigned int m68k_read_immediate_32(unsigned int a)
{
    return m68k_read_immediate_16(a) << 16 | m68k_read_immediate_16(a + 2);
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

/* True while Musashi's write reaches the bus: ROM-write reports name its PC. */
static bool cpu_writing;

static bool cpu_origin(uint32_t *pc, m68k_regs *r)
{
    if (!cpu_writing) return false;
    *pc = m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF;
    for (int i = 0; i < 8; i++) {
        r->d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        r->a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    r->sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    return true;
}

void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    clr_read(a, 8);
    emit(true, 8, a, v & 0xFF);
    cpu_writing = true;
    nmd_bus_wr8_raw(a, (uint8_t)v);
    cpu_writing = false;
}

void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    clr_read(a, 16);
    emit(true, 16, a, v & 0xFFFF);
    cpu_writing = true;
    nmd_bus_wr16_raw(a, (uint16_t)v);
    cpu_writing = false;
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
/* Registers                                                                 */
/* ------------------------------------------------------------------------ */

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
    m68k_set_reg(M68K_REG_SR, M68K.sr); /* first: it selects which stack pointer A7 is */
    for (int i = 0; i < 8; i++) {
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), M68K.d[i]);
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), M68K.a[i]);
    }
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
    if (level == 6) {
        vint_pending = false;
        vdp_vint_ack();
        vint_count++;
        if (have_idle && irq_depth == 0 && !idle_polled) {
            if (!lag_frames) first_lag_vint = vint_count;
            lag_frames++;
            if (nmd_trace_enabled())
                nmd_trace_comment("lag VINT %llu (main code at %06X had not reached the idle loop)",
                                  (unsigned long long)vint_count, m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF);
        }
        if (irq_depth == 0) idle_polled = false;
    }
    if (level == 4) hint_pending = false;
    update_irq();
    in_exception = true; /* drop the vector fetch; stack pushes are dropped by range */
    if (irq_depth < 16) irq_levels[irq_depth] = (unsigned)level;
    irq_depth++;
    if (nmd_trace_enabled()) nmd_trace_comment("irq %d", level);
    return M68K_INT_ACK_AUTOVECTOR;
}

static void on_rte(void)
{
    if (!frames_mode || irq_depth <= 0) return;
    irq_depth--;
    unsigned level = irq_depth < 16 ? irq_levels[irq_depth] : 0;
    if (nmd_trace_enabled()) nmd_trace_comment("rte %u", level);
    if (level == 6 && vint_target && vint_count >= vint_target) {
        pending_action = ACT_RETURNED;
        m68k_end_timeslice();
    }
}

static int on_tas(void) { return 0; }

static void park(action a, uint32_t pc)
{
    pending_action = a;
    action_pc = pc;
    m68k_set_reg(M68K_REG_PC, TRAMPOLINE);
    m68k_end_timeslice();
}

static void capture_state(size_t i)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s.%04u.nmds", capture_dir, captures[i].name, captures[i].count);
    m68k_regs saved = M68K;
    regs_to_engine();
    if (!nmd_state_save(path)) nmd_fatal("oracle: cannot write %s", path);
    M68K = saved;
    captures[i].count++;
    captured_total++;
}

static void on_instr(unsigned int pc)
{
    in_exception = false;
    pc &= 0xFFFFFF;
    if (pc == SENTINEL) {
        if (pending_action == ACT_NONE) pending_action = ACT_RETURNED;
        m68k_end_timeslice();
        return;
    }
    if (pc == TRAMPOLINE) return;
    {
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7) & 0xFFFFFF;
        if (sp < min_sp) min_sp = sp;
    }

    /* CLR.b/w/l <ea> with a memory operand: 0100 0010 ss mmm rrr, ss != 11, mmm != 000 */
    uint16_t op = nmd_bus_rd16_raw(pc & ~1u);
    /* MOVEM.w/.l <list>,-(A7): 0100 1000 1s 100 111 */
    movem_lo = 0;
    if ((op & 0xFFBF) == 0x48A7) {
        uint16_t mask = nmd_bus_rd16_raw((pc + 2) & ~1u);
        unsigned n = 0;
        for (unsigned b = 0; b < 16; b++) n += (mask >> b) & 1;
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7) & 0xFFFFFF;
        movem_lo = (sp - n * ((op & 0x40) ? 4u : 2u)) & 0xFFFFFF;
        if (movem_lo < min_sp) min_sp = movem_lo;
    }
    clr_read_pending = (op & 0xFF00) == 0x4200 && (op & 0x00C0) != 0x00C0 && (op & 0x0038) != 0;

    bool in_idle = have_idle && pc >= idle_lo && pc < idle_hi;
    if (have_idle && irq_depth == 0 && !in_idle) idle_have_last = false;
    if (coverage && pc / 2 < coverage_words) coverage[pc / 2] = 1;
    for (size_t i = 0; i < ncaptures; i++)
        if (captures[i].addr == pc && captures[i].count < capture_max &&
            captures[i].hits++ % capture_every == 0) capture_state(i);

    if (!divert_to_c) return;
    if (find_routine(pc)) {
        park(ACT_CALL_C, pc);
        return;
    }
    if (in_idle && irq_depth == 0) {
        if (pc == idle_lo) {
            if (idle_armed) { idle_armed = false; park(ACT_IDLE, pc); return; }
            idle_armed = true;
        }
    } else if (irq_depth == 0) {
        idle_armed = false;
    }
}

/* ------------------------------------------------------------------------ */
/* Setup, symbols, options                                                   */
/* ------------------------------------------------------------------------ */

void oracle_setup(void)
{
    const uint8_t *rom = nmd_rom(NULL);
    uint32_t ssp = be32(rom);
    stack_top = ssp & 0xFFFFFF;
    stack_fixed = false;
    reset_started = false;
    min_sp = stack_top;
    have_idle = idle_have_last = in_exception = clr_read_pending = false;
    irq_depth = 0;
    cpu_writing = false;
    nmd_set_cpu_origin(cpu_origin);
    frames_mode = hint_pending = vint_pending = false;
    vint_count = vint_target = 0;
    pending_action = ACT_NONE;
    divert_to_c = idle_armed = false;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(on_int_ack);
    m68k_set_rte_instr_callback(on_rte);
    m68k_set_tas_instr_callback(on_tas);
    m68k_set_instr_hook_callback(on_instr);
    muted = true;
    m68k_pulse_reset();
    muted = false;
}

void oracle_set_stack(uint32_t lo, uint32_t hi)
{
    stack_lo = lo & 0xFFFFFF;
    stack_hi = hi & 0xFFFFFF;
    stack_fixed = true;
}

uint32_t oracle_min_sp(void) { return min_sp; }
void oracle_reset_min_sp(void) { min_sp = stack_top; }

void oracle_set_idle(uint32_t lo, uint32_t hi)
{
    idle_lo = lo & 0xFFFFFF;
    idle_hi = hi & 0xFFFFFF;
    have_idle = true;
}

bool oracle_load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        sym s;
        if (sscanf(line, "%x %63s", &s.addr, s.name) != 2) continue;
        sym *grown = realloc(syms, (nsyms + 1) * sizeof *syms);
        if (!grown) { fclose(f); return false; }
        syms = grown;
        syms[nsyms++] = s;
    }
    fclose(f);
    return true;
}

bool oracle_resolve(const char *text, uint32_t *addr, uint32_t *next, bool *has_next)
{
    if (has_next) *has_next = false;
    for (size_t i = 0; i < nsyms; i++) {
        if (strcmp(syms[i].name, text) != 0) continue;
        *addr = syms[i].addr;
        uint32_t best = 0xFFFFFFFF;
        for (size_t j = 0; j < nsyms; j++)
            if (syms[j].addr > syms[i].addr && syms[j].addr < best) {
                best = syms[j].addr;
                if (has_next) *has_next = true;
            }
        if (next) *next = best;
        return true;
    }
    char *end;
    unsigned long v = strtoul(text, &end, 16);
    if (*text == 0 || *end != 0) return false;
    *addr = (uint32_t)v & 0xFFFFFF;
    return true;
}

void oracle_set_pad_script(const uint16_t (*script)[2], size_t frames)
{
    pad_script = script;
    pad_frames = frames;
}

void oracle_enable_coverage(void)
{
    size_t size;
    nmd_rom(&size);
    coverage_words = (size + 1) / 2;
    free(coverage);
    coverage = calloc(coverage_words, 1);
}

bool oracle_write_coverage(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    for (size_t i = 0; coverage && i < coverage_words; i++)
        if (coverage[i]) fprintf(f, "%06zX\n", i * 2);
    return fclose(f) == 0;
}

bool oracle_add_capture(uint32_t addr, const char *name)
{
    if (ncaptures >= MAX_CAPTURES) return false;
    captures[ncaptures].addr = addr & 0xFFFFFF;
    snprintf(captures[ncaptures].name, sizeof captures[ncaptures].name, "%s", name);
    captures[ncaptures].count = 0;
    captures[ncaptures].hits = 0;
    ncaptures++;
    return true;
}

void oracle_set_capture(const char *dir, unsigned max_per_addr)
{
    snprintf(capture_dir, sizeof capture_dir, "%s", dir);
    capture_max = max_per_addr;
}

void oracle_set_capture_every(unsigned every)
{
    capture_every = every ? every : 1;
}

unsigned oracle_captured(void) { return captured_total; }

uint64_t oracle_lag_frames(uint64_t *first_vint)
{
    if (first_vint) *first_vint = first_lag_vint;
    return lag_frames;
}

/* ------------------------------------------------------------------------ */
/* Frames mode                                                               */
/* ------------------------------------------------------------------------ */

static void sample_pads(uint64_t frame)
{
    for (int p = 0; p < 2; p++) {
        uint16_t v = 0;
        if (pad_frames) v = pad_script[frame < pad_frames ? frame : pad_frames - 1][p];
        NMD.io.pad[p] = v;
    }
}

bool oracle_run_frames(uint64_t frames, uint32_t resume_pc)
{
    frames_mode = true;
    vint_target = frames;
    idle_polled = false;
    lag_frames = first_lag_vint = 0;
    divert_to_c = false;
    muted = true;
    m68k_pulse_reset();
    muted = false;
    if (resume_pc) {
        regs_from_engine();
        m68k_set_reg(M68K_REG_PC, resume_pc);
    }
    sample_pads(0);
    uint64_t mclk_done = 0, cycles_done = 0;
    for (uint64_t frame = 0; pending_action != ACT_RETURNED; frame++) {
        /* Games run long stretches with VINT masked (checksums, decompression):
         * allow a generous number of video frames per requested VINT. */
        if (frame > frames * 4 + 600) {
            fprintf(stderr, "oracle: %llu VINTs taken after %llu frames (VINT disabled?)\n",
                    (unsigned long long)vint_count, (unsigned long long)frame);
            return false;
        }
        vdp_begin_frame();
        unsigned active = vdp_active_lines();
        unsigned lines = NMD.cfg.video == NMD_VIDEO_PAL ? LINES_PAL : LINES_NTSC;
        for (unsigned line = 0; line < lines && pending_action != ACT_RETURNED; line++) {
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
            while (cycles_done < target && pending_action != ACT_RETURNED)
                cycles_done += (uint64_t)m68k_execute((int)(target - cycles_done));
        }
    }
    regs_to_engine();
    frames_mode = false;
    pending_action = ACT_NONE;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Routine calls and hybrid execution                                        */
/* ------------------------------------------------------------------------ */

static uint32_t rd32_raw(uint32_t a) { return (uint32_t)nmd_bus_rd16_raw(a) << 16 | nmd_bus_rd16_raw(a + 2); }

static void wr32_raw(uint32_t a, uint32_t v)
{
    nmd_bus_wr16_raw(a, (uint16_t)(v >> 16));
    nmd_bus_wr16_raw(a + 2, (uint16_t)v);
}

/* Runs addr from M68K's registers until it returns to the sentinel. */
static bool run_routine(uint32_t addr, bool irq, uint64_t max_cycles, bool divert, bool sentinel)
{
    /* Musashi is not re-entrant: keep the context of an enclosing run. */
    unsigned ctx_size = m68k_context_size();
    void *outer = malloc(ctx_size);
    if (!outer) nmd_fatal("oracle: out of memory");
    m68k_get_context(outer);
    bool outer_divert = divert_to_c;
    action outer_action = pending_action;
    if (irq) { if (irq_depth < 16) irq_levels[irq_depth] = 0; irq_depth++; }

    regs_from_engine();
    uint32_t sp = (uint32_t)m68k_get_reg(NULL, M68K_REG_A7);
    if (irq) {                           /* exception frame: SR, then PC */
        sp -= 6;
        nmd_bus_wr16_raw(sp, M68K.sr);
        wr32_raw(sp + 2, SENTINEL);
    } else if (sentinel) {
        sp -= 4;
        wr32_raw(sp, SENTINEL);
    }
    m68k_set_reg(M68K_REG_A7, sp);
    m68k_set_reg(M68K_REG_PC, addr & 0xFFFFFF);
    divert_to_c = divert;
    pending_action = ACT_NONE;

    bool ok = true;
    uint64_t cycles = 0;
    for (;;) {
        cycles += (uint64_t)m68k_execute(100000);
        action a = pending_action;
        uint32_t at = action_pc; /* locals: nested runs reuse the globals */
        pending_action = ACT_NONE;
        if (a == ACT_RETURNED) break;
        if (a == ACT_CALL_C) {
            nmd_routine_fn fn = find_routine(at);
            regs_to_engine();
            if (nmd_trace_enabled()) nmd_trace_comment("asm->c %06X", at);
            nmd_run_c(at, fn);
            regs_from_engine();
            /* The C returns as by RTS. */
            uint32_t a7 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A7);
            m68k_set_reg(M68K_REG_PC, rd32_raw(a7) & 0xFFFFFF);
            m68k_set_reg(M68K_REG_A7, a7 + 4);
            cycles = 0;
            continue;
        }
        if (a == ACT_IDLE) {
            regs_to_engine();
            wait_vblank();
            regs_from_engine();
            m68k_set_reg(M68K_REG_PC, at);
            cycles = 0;
            continue;
        }
        if (cycles > max_cycles) {
            ok = false;
            nmd_fatal("oracle: routine %06X ran %llu cycles without returning (PC %06X)%s", addr,
                      (unsigned long long)cycles, m68k_get_reg(NULL, M68K_REG_PC),
                      divert ? "; is an idle loop missing?" : "");
        }
    }
    regs_to_engine();

    if (irq) irq_depth--;
    divert_to_c = outer_divert;
    pending_action = outer_action;
    m68k_set_context(outer);
    free(outer);
    return ok;
}

bool oracle_call(uint32_t addr, bool irq, uint64_t max_cycles)
{
    return run_routine(addr, irq, max_cycles, false, true);
}

static bool hybrid_handler(uint32_t addr, bool is_interrupt)
{
    if (nmd_trace_enabled()) nmd_trace_comment("c->asm %06X", addr);
    /* The reset routine never returns: no sentinel, A7 at the initial SSP as on hardware
     * (a sentinel would shift the stack filter by 4 bytes against the C's). */
    bool reset = !is_interrupt && irq_depth == 0 && !reset_started &&
                 (addr & 0xFFFFFF) == (be32(nmd_rom(NULL) + 4) & 0xFFFFFF);
    if (reset) reset_started = true;
    return run_routine(addr, is_interrupt, hybrid_max_cycles, true, !reset);
}

bool oracle_parse_rom_writes(const char *text, nmd_rom_writes *out)
{
    if (!text) return false;
    if (!strcmp(text, "ignore")) { *out = NMD_ROM_WRITES_IGNORE; return true; }
    if (!strcmp(text, "fatal"))  { *out = NMD_ROM_WRITES_FATAL;  return true; }
    return false;
}

bool oracle_parse_region(const char *text, nmd_video *video, bool *overseas)
{
    if (!text) return false;
    if (!strcmp(text, "jp")) { *video = NMD_VIDEO_NTSC; *overseas = false; return true; }
    if (!strcmp(text, "us")) { *video = NMD_VIDEO_NTSC; *overseas = true;  return true; }
    if (!strcmp(text, "eu")) { *video = NMD_VIDEO_PAL;  *overseas = true;  return true; }
    return false;
}

void oracle_install_hybrid(uint64_t max_cycles_per_slice)
{
    if (max_cycles_per_slice) hybrid_max_cycles = max_cycles_per_slice;
    nmd_set_missing_routine_handler(hybrid_handler);
}
