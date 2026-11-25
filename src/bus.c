/* bus.c - 68000 bus: memory map, access tracing, special instructions. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"

m68k_regs M68K;

static nmd_missing_routine_handler missing_handler;

void nmd_set_missing_routine_handler(nmd_missing_routine_handler h) { missing_handler = h; }

bool nmd_run_missing_routine(uint32_t addr, bool is_interrupt)
{
    return missing_handler && missing_handler(addr & 0xFFFFFF, is_interrupt);
}

/* ------------------------------------------------------------------------ */
/* Side-effect read replay                                                   */
/* ------------------------------------------------------------------------ */

static nmd_trace_line *replay;
static size_t          replay_len, replay_pos;
static bool            replay_active;

static bool is_device(uint32_t addr) { return addr >= 0xA00000 && addr <= 0xDFFFFF; }

bool nmd_replay_open(FILE *f)
{
    nmd_replay_close();
    char line[256];
    nmd_trace_line l;
    while (fgets(line, sizeof line, f)) {
        int k = nmd_trace_parse(line, &l);
        if (k < 0) { nmd_replay_close(); return false; }
        if (k == 0 || l.write || l.bus != NMD_TRACE_M68K || !is_device(l.addr)) continue;
        nmd_trace_line *grown = realloc(replay, (replay_len + 1) * sizeof *replay);
        if (!grown) { nmd_replay_close(); return false; }
        replay = grown;
        replay[replay_len++] = l;
    }
    replay_active = true;
    return true;
}

void nmd_replay_close(void)
{
    free(replay);
    replay = NULL;
    replay_len = replay_pos = 0;
    replay_active = false;
}

size_t nmd_replay_remaining(void) { return replay_len - replay_pos; }

static uint32_t replayed(uint32_t addr, unsigned bits, uint32_t actual)
{
    if (!replay_active || !is_device(addr)) return actual;
    if (replay_pos >= replay_len)
        nmd_fatal("replay: C read%u %06X, but the recorded trace has no more device reads", bits, addr);
    const nmd_trace_line *r = &replay[replay_pos++];
    if (r->bits != bits || r->addr != addr)
        nmd_fatal("replay: C read%u %06X, recorded read%u %06X (device read #%zu)",
                  bits, addr, r->bits, r->addr, replay_pos);
    return r->value;
}

/* ------------------------------------------------------------------------ */
/* Origin of accesses; ROM writes without a mapper                           */
/* ------------------------------------------------------------------------ */

static nmd_cpu_origin_fn cpu_origin;
static uint32_t          c_routine;      /* innermost C routine, if c_depth > 0 */
static unsigned          c_depth;

void nmd_set_cpu_origin(nmd_cpu_origin_fn fn) { cpu_origin = fn; }

void nmd_run_c(uint32_t addr, nmd_routine_fn fn)
{
    uint32_t outer = c_routine;
    c_routine = addr & 0xFFFFFF;
    c_depth++;
    fn();
    c_depth--;
    c_routine = outer;
}

static nmd_rom_write_site rom_sites[NMD_MAX_ROM_WRITE_SITES];
static size_t             rom_nsites;
static uint64_t           rom_other, rom_total;

size_t nmd_rom_write_sites(const nmd_rom_write_site **sites, uint64_t *other)
{
    if (sites) *sites = rom_sites;
    if (other) *other = rom_other;
    return rom_nsites;
}

uint64_t nmd_rom_write_total(void) { return rom_total; }

void nmd_rom_writes_clear(void) { rom_nsites = 0; rom_other = rom_total = 0; }

void nmd_rom_writes_report(FILE *f, const char *who)
{
    if (!rom_total) return;
    fprintf(f, "%s: %llu ignored ROM write(s) at %zu address(es)%s\n", who,
            (unsigned long long)rom_total, rom_nsites, rom_other ? " and more" : "");
    for (size_t i = 0; i < rom_nsites; i++) {
        const nmd_rom_write_site *r = &rom_sites[i];
        fprintf(f, "  %06X x%llu, first write%u %0*X from ", r->addr, (unsigned long long)r->count,
                r->first_bits, r->first_bits / 4, r->first_value);
        if (!r->first_known)      fprintf(f, "unknown origin\n");
        else if (r->first_from_c) fprintf(f, "C routine %06X\n", r->first_origin);
        else                      fprintf(f, "PC %06X\n", r->first_origin);
    }
    if (rom_other)
        fprintf(f, "  %llu more write(s) at addresses beyond the first %d\n",
                (unsigned long long)rom_other, NMD_MAX_ROM_WRITE_SITES);
}

static void rom_write(unsigned bits, uint32_t addr, uint16_t v)
{
    uint32_t pc = 0;
    m68k_regs r = M68K;
    bool from_cpu = cpu_origin && cpu_origin(&pc, &r);
    bool known = from_cpu || c_depth > 0;
    if (!from_cpu) pc = c_routine;

    if (NMD.cfg.rom_writes == NMD_ROM_WRITES_FATAL) {
        char origin[48];
        if (from_cpu)   snprintf(origin, sizeof origin, "PC %06X", pc);
        else if (known) snprintf(origin, sizeof origin, "C routine %06X", pc);
        else            snprintf(origin, sizeof origin, "unknown origin");
        nmd_fatal("68k write%u to ROM %06X <- %0*X (no mapper chip claims it; strict ROM writes) from %s; "
                  "D0-D7 %08X %08X %08X %08X %08X %08X %08X %08X "
                  "A0-A7 %08X %08X %08X %08X %08X %08X %08X %08X SR %04X",
                  bits, addr, (int)bits / 4, v, origin,
                  r.d[0], r.d[1], r.d[2], r.d[3], r.d[4], r.d[5], r.d[6], r.d[7],
                  r.a[0], r.a[1], r.a[2], r.a[3], r.a[4], r.a[5], r.a[6], r.a[7], r.sr);
    }
    rom_total++;
    for (size_t i = 0; i < rom_nsites; i++)
        if (rom_sites[i].addr == addr) { rom_sites[i].count++; return; }
    if (rom_nsites == NMD_MAX_ROM_WRITE_SITES) { rom_other++; return; }
    rom_sites[rom_nsites++] = (nmd_rom_write_site){
        .addr = addr, .count = 1, .first_bits = bits, .first_value = v,
        .first_from_c = !from_cpu && known, .first_known = known, .first_origin = pc,
    };
}

/* ------------------------------------------------------------------------ */

static const nmd_chip *find_chip(uint32_t addr)
{
    for (size_t i = 0; i < NMD.chip_count; i++)
        if (addr >= NMD.chips[i].start && addr <= NMD.chips[i].end)
            return &NMD.chips[i];
    return NULL;
}

static uint8_t rom_rd8(uint32_t addr)
{
    if (addr >= NMD.rom_size)
        nmd_fatal("68k read8 beyond end of ROM: %06X (ROM size %zX)", addr, NMD.rom_size);
    return NMD.rom[addr];
}

/* 8-bit read, no tracing. */
static uint8_t bus_rd8(uint32_t addr)
{
    addr &= 0xFFFFFF;
    const nmd_chip *c = find_chip(addr);
    if (c) {
        if (!c->rd8) nmd_fatal("chip %s: no rd8 handler for %06X", c->name, addr);
        return c->rd8(c->user, addr);
    }
    if (addr <= HW_ROM_END)      return rom_rd8(addr);
    if (addr >= 0xE00000)        return NMD.ram[addr & 0xFFFF];
    if (addr >= 0xC00000 && addr <= 0xC0001F) {
        if (addr >= 0xC00010)    nmd_fatal("68k read8 from write-only PSG/unused VDP port %06X", addr);
        uint16_t w = vdp_rd16(addr & ~1u);
        return (addr & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (addr >= HW_Z80_START && addr <= HW_Z80_END) return z80_area_rd8(addr);
    if (addr >= 0xA10000 && addr <= 0xA1001F)       return io_rd8(addr);
    if (addr == HW_Z80_BUSREQ || addr == HW_Z80_BUSREQ + 1 ||
        addr == HW_Z80_RESET  || addr == HW_Z80_RESET + 1) {
        uint16_t w = z80_ctrl_rd16(addr & ~1u);
        return (addr & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    nmd_fatal("68k read8 from unmapped address %06X", addr);
    return 0;
}

uint16_t nmd_bus_rd16_raw(uint32_t addr)
{
    addr &= 0xFFFFFF;
    if (addr & 1) nmd_fatal("68k address error: read16 at odd address %06X", addr);
    const nmd_chip *c = find_chip(addr);
    if (c) {
        if (!c->rd16) nmd_fatal("chip %s: no rd16 handler for %06X", c->name, addr);
        return c->rd16(c->user, addr);
    }
    if (addr <= HW_ROM_END) {
        if (addr + 1 >= NMD.rom_size)
            nmd_fatal("68k read16 beyond end of ROM: %06X (ROM size %zX)", addr, NMD.rom_size);
        return be16(NMD.rom + addr);
    }
    if (addr >= 0xE00000) return be16(NMD.ram + (addr & 0xFFFF));
    if (addr >= 0xC00000 && addr <= 0xC0000F) return vdp_rd16(addr);
    if (addr == HW_Z80_BUSREQ || addr == HW_Z80_RESET) return z80_ctrl_rd16(addr);
    /*
     * 8-bit devices: a word read performs one device read and returns the
     * byte on both halves (I/O registers live at odd addresses, Z80 space
     * returns the even byte). To be confirmed against the oracle.
     */
    if (addr >= HW_Z80_START && addr <= HW_Z80_END) {
        uint8_t b = bus_rd8(addr);
        return (uint16_t)(b << 8 | b);
    }
    if (addr >= 0xA10000 && addr <= 0xA1001F) {
        uint8_t b = bus_rd8(addr + 1);
        return (uint16_t)(b << 8 | b);
    }
    nmd_fatal("68k read16 from unmapped address %06X", addr);
    return 0;
}

static void bus_wr8(uint32_t addr, uint8_t v)
{
    addr &= 0xFFFFFF;
    const nmd_chip *c = find_chip(addr);
    if (c) {
        if (!c->wr8) nmd_fatal("chip %s: no wr8 handler for %06X", c->name, addr);
        c->wr8(c->user, addr, v);
        return;
    }
    if (addr >= 0xE00000) { NMD.ram[addr & 0xFFFF] = v; return; }
    if (addr == HW_PSG || addr == HW_PSG + 2 || addr == HW_PSG + 4 || addr == HW_PSG + 6) {
        psg_write(v);
        return;
    }
    if (addr >= 0xC00000 && addr <= 0xC00007) {
        /* Byte writes to VDP ports put the byte on both halves of the bus. */
        vdp_wr16(addr & ~1u, (uint16_t)(v << 8 | v));
        return;
    }
    if (addr >= HW_Z80_START && addr <= HW_Z80_END) { z80_area_wr8(addr, v); return; }
    if (addr >= 0xA10000 && addr <= 0xA1001F)       { io_wr8(addr, v); return; }
    if (addr == HW_Z80_BUSREQ || addr == HW_Z80_RESET) {
        z80_ctrl_wr16(addr, (uint16_t)(v << 8));
        return;
    }
    if (addr == HW_Z80_BUSREQ + 1 || addr == HW_Z80_RESET + 1) return; /* ignored half */
    if (addr <= HW_ROM_END) { rom_write(8, addr, v); return; }
    nmd_fatal("68k write8 to unmapped address %06X <- %02X", addr, v);
}

static void bus_wr16(uint32_t addr, uint16_t v)
{
    addr &= 0xFFFFFF;
    if (addr & 1) nmd_fatal("68k address error: write16 at odd address %06X", addr);
    const nmd_chip *c = find_chip(addr);
    if (c) {
        if (!c->wr16) nmd_fatal("chip %s: no wr16 handler for %06X", c->name, addr);
        c->wr16(c->user, addr, v);
        return;
    }
    if (addr >= 0xE00000) {
        uint8_t *p = NMD.ram + (addr & 0xFFFF);
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
        return;
    }
    if (addr >= 0xC00000 && addr <= 0xC00007) { vdp_wr16(addr, v); return; }
    if (addr == HW_PSG - 1) { psg_write((uint8_t)v); return; }
    if (addr == HW_Z80_BUSREQ || addr == HW_Z80_RESET) { z80_ctrl_wr16(addr, v); return; }
    /* 8-bit devices: one device write (Z80 space: high byte; I/O: low byte). */
    if (addr >= HW_Z80_START && addr <= HW_Z80_END) { bus_wr8(addr, (uint8_t)(v >> 8)); return; }
    if (addr >= 0xA10000 && addr <= 0xA1001F)       { bus_wr8(addr + 1, (uint8_t)v);    return; }
    if (addr <= HW_ROM_END) { rom_write(16, addr, v); return; }
    nmd_fatal("68k write16 to unmapped address %06X <- %04X", addr, v);
}

uint8_t nmd_bus_rd8_raw(uint32_t addr)              { return bus_rd8(addr); }
void    nmd_bus_wr8_raw(uint32_t addr, uint8_t v)   { bus_wr8(addr, v); }
void    nmd_bus_wr16_raw(uint32_t addr, uint16_t v) { bus_wr16(addr, v); }

/* ------------------------------------------------------------------------ */
/* Public contract                                                           */
/* ------------------------------------------------------------------------ */

uint8_t m68k_rd8(uint32_t addr)
{
    addr &= 0xFFFFFF;
    uint8_t v = (uint8_t)replayed(addr, 8, bus_rd8(addr));
    nmd_trace_access(NMD_TRACE_M68K, false, 8, addr, v);
    return v;
}

uint16_t m68k_rd16(uint32_t addr)
{
    addr &= 0xFFFFFF;
    uint16_t v = (uint16_t)replayed(addr, 16, nmd_bus_rd16_raw(addr));
    nmd_trace_access(NMD_TRACE_M68K, false, 16, addr, v);
    return v;
}

uint32_t m68k_rd32(uint32_t addr)
{
    uint32_t hi = m68k_rd16(addr);
    uint32_t lo = m68k_rd16(addr + 2);
    return hi << 16 | lo;
}

void m68k_wr8(uint32_t addr, uint8_t v)
{
    addr &= 0xFFFFFF;
    nmd_trace_access(NMD_TRACE_M68K, true, 8, addr, v);
    bus_wr8(addr, v);
}

void m68k_wr16(uint32_t addr, uint16_t v)
{
    addr &= 0xFFFFFF;
    nmd_trace_access(NMD_TRACE_M68K, true, 16, addr, v);
    bus_wr16(addr, v);
}

void m68k_wr32(uint32_t addr, uint32_t v)
{
    m68k_wr16(addr, (uint16_t)(v >> 16));
    m68k_wr16(addr + 2, (uint16_t)v);
}

void m68k_wr32_predec(uint32_t addr, uint32_t v)
{
    m68k_wr16(addr + 2, (uint16_t)v);
    m68k_wr16(addr, (uint16_t)(v >> 16));
}

void m68k_clr8(uint32_t addr)  { (void)m68k_rd8(addr);  m68k_wr8(addr, 0); }
void m68k_clr16(uint32_t addr) { (void)m68k_rd16(addr); m68k_wr16(addr, 0); }
void m68k_clr32(uint32_t addr) { (void)m68k_rd32(addr); m68k_wr32(addr, 0); }

uint8_t m68k_tas(uint32_t addr)
{
    return m68k_rd8(addr); /* write-back does not complete on the Mega Drive */
}

/* ------------------------------------------------------------------------ */
/* Routines by address                                                       */
/* ------------------------------------------------------------------------ */

nmd_routine_fn find_routine(uint32_t addr)
{
    addr &= 0xFFFFFF;
    const nmd_game *g = NMD.game;
    if (!g) return NULL;
    for (size_t i = 0; i < g->routine_count; i++)
        if (g->routines[i].addr == addr) return g->routines[i].fn;
    return NULL;
}

static const char *routine_name(uint32_t addr)
{
    const nmd_game *g = NMD.game;
    for (size_t i = 0; g && i < g->routine_count; i++)
        if (g->routines[i].addr == addr && g->routines[i].name) return g->routines[i].name;
    return "?";
}

void call_routine(uint32_t addr)
{
    addr &= 0xFFFFFF;
    nmd_routine_fn fn = find_routine(addr);
    if (!fn) {
        if (nmd_run_missing_routine(addr, false)) return;
        nmd_fatal("call_routine: no C for 68k address %06X", addr);
    }
    if (nmd_trace_enabled()) nmd_trace_comment("call %06X %s", addr, routine_name(addr));
    nmd_run_c(addr, fn);
    if (nmd_trace_enabled()) nmd_trace_comment("ret %06X", addr);
}

void jump_routine(uint32_t addr)
{
    addr &= 0xFFFFFF;
    nmd_routine_fn fn = find_routine(addr);
    if (!fn) {
        if (nmd_run_missing_routine(addr, false)) return;
        nmd_fatal("jump_routine: no C for 68k address %06X", addr);
    }
    if (nmd_trace_enabled()) nmd_trace_comment("jump %06X %s", addr, routine_name(addr));
    nmd_run_c(addr, fn);
}
