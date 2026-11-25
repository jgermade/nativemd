/*
 * test_core - unit tests for the bus, VDP ports/DMA, I/O, traces and state
 * files. Accesses are made directly from the test (no game coroutine).
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nativemd/nativemd.h"

static int failures;
static jmp_buf fatal_jmp;
static char fatal_msg[512];

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void on_fatal(const char *msg)
{
    snprintf(fatal_msg, sizeof fatal_msg, "%s", msg);
    longjmp(fatal_jmp, 1);
}

/* Evaluates stmt and reports whether it raised a fatal error. */
#define EXPECT_FATAL(stmt) do { \
    fatal_msg[0] = 0; \
    if (setjmp(fatal_jmp) == 0) { stmt; failures++; \
        fprintf(stderr, "FAIL %s:%d: expected fatal from %s\n", __FILE__, __LINE__, #stmt); } \
    } while (0)

static void dummy(void) {}
static const nmd_routine routines[] = { { 0x000200, dummy, "Dummy" } };
static const nmd_game game = { .name = "core", .routines = routines, .routine_count = 1 };

static uint8_t rom[0x1000];

static void setup(void)
{
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0x100, "SEGA MEGA DRIVE ", 16);
    rom[0x004 + 2] = 0x02; /* reset PC 0x200 */
    for (int i = 0; i < 16; i++) { rom[0x800 + 2 * i] = (uint8_t)(i >> 4); rom[0x801 + 2 * i] = (uint8_t)(i * 0x11); }
    nmd_config cfg = { .video = NMD_VIDEO_NTSC };
    if (!nmd_init(&cfg, &game, rom, sizeof rom)) { fprintf(stderr, "init failed\n"); exit(2); }
}

static void test_ram(void)
{
    setup();
    m68k_wr16(0xFF1000, 0x1234);
    CHECK(m68k_rd8(0xFF1000) == 0x12 && m68k_rd8(0xFF1001) == 0x34, "big-endian RAM");
    m68k_wr32(0xFF1004, 0xDEADBEEF);
    CHECK(m68k_rd32(0xFF1004) == 0xDEADBEEF, "rd32");
    CHECK(m68k_rd16(0xE01004) == 0xDEAD, "RAM mirror at E00000");
    m68k_wr32_predec(0xFF1008, 0x01020304);
    CHECK(m68k_rd32(0xFF1008) == 0x01020304, "wr32_predec value");
    m68k_clr16(0xFF1000);
    CHECK(m68k_rd16(0xFF1000) == 0, "clr16");
    m68k_wr8(0xFF1010, 0x05);
    CHECK(m68k_tas(0xFF1010) == 0x05 && m68k_rd8(0xFF1010) == 0x05, "TAS does not write back");
    M68K_RMW16(0xFF1012, v + 3);
    CHECK(m68k_rd16(0xFF1012) == 3, "RMW16");
    CHECK(m68k_rd16(0x000800) == 0x0000 && m68k_rd16(0x000802) == 0x0011, "ROM read");
}

static void test_fatal(void)
{
    setup();
    nmd_set_fatal_handler(on_fatal);
    EXPECT_FATAL(m68k_rd16(0xFF0001));
    CHECK(strstr(fatal_msg, "odd") != NULL, "odd-address message: %s", fatal_msg);
    EXPECT_FATAL(m68k_wr16(0x000100, 0));
    EXPECT_FATAL(m68k_rd16(0x400000));
    EXPECT_FATAL(m68k_rd8(0x002000)); /* beyond the 4 KB test ROM */
    EXPECT_FATAL(m68k_wr8(0xA00000, 1)); /* Z80 RAM without BUSREQ */
    EXPECT_FATAL(call_routine(0x000300));
    CHECK(strstr(fatal_msg, "000300") != NULL, "missing routine message: %s", fatal_msg);
    nmd_set_fatal_handler(NULL);
}

static void test_vdp(void)
{
    setup();
    /* Register write and CRAM write with autoincrement. */
    m68k_wr16(HW_VDP_CTRL, VDP_REG(15, 2));
    m68k_wr32(HW_VDP_CTRL, VDP_CRAM_WRITE(0x0002));
    m68k_wr16(HW_VDP_DATA, 0x0FFF);
    m68k_wr16(HW_VDP_DATA, 0x0246);
    CHECK(nmd_cram()[1] == 0x0EEE, "CRAM masks to 9 bits: %04X", nmd_cram()[1]);
    CHECK(nmd_cram()[2] == 0x0246, "CRAM autoinc: %04X", nmd_cram()[2]);

    /* VRAM write and read back. */
    m68k_wr32(HW_VDP_CTRL, VDP_VRAM_WRITE(0x1234 & ~1u));
    m68k_wr16(HW_VDP_DATA, 0xA55A);
    CHECK(nmd_vram()[0x1234] == 0xA5 && nmd_vram()[0x1235] == 0x5A, "VRAM write");
    m68k_wr32(HW_VDP_CTRL, VDP_CMD(VDP_CODE_VRAM_RD, 0x1234));
    CHECK(m68k_rd16(HW_VDP_DATA) == 0xA55A, "VRAM read");

    /* Address bits 14-15 come from the second word. */
    m68k_wr32(HW_VDP_CTRL, VDP_VRAM_WRITE(0xF000));
    m68k_wr16(HW_VDP_DATA, 0x1122);
    CHECK(nmd_vram()[0xF000] == 0x11, "VRAM high address");

    /* VSRAM. */
    m68k_wr32(HW_VDP_CTRL, VDP_VSRAM_WRITE(0x0000));
    m68k_wr16(HW_VDP_DATA, 0xFFFF);
    CHECK(nmd_vsram()[0] == 0x07FF, "VSRAM mask %04X", nmd_vsram()[0]);

    /* 68k -> VRAM DMA from ROM $800, 16 words. */
    m68k_wr16(HW_VDP_CTRL, VDP_REG(1, 0x14));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(19, 16));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(20, 0));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(21, (0x800 >> 1) & 0xFF));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(22, (0x800 >> 9) & 0xFF));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(23, 0));
    m68k_wr32(HW_VDP_CTRL, VDP_CMD(VDP_CODE_VRAM_WR | VDP_CODE_DMA, 0x2000));
    CHECK(memcmp(nmd_vram() + 0x2000, rom + 0x800, 32) == 0, "68k->VRAM DMA");
    CHECK(nmd_vdp_regs()[19] == 0 && nmd_vdp_regs()[20] == 0, "DMA length cleared");

    /* VRAM fill: 0x20 bytes of 0x77 after the first word. */
    m68k_wr16(HW_VDP_CTRL, VDP_REG(15, 1));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(19, 0x20));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(20, 0));
    m68k_wr16(HW_VDP_CTRL, VDP_REG(23, 0x80));
    m68k_wr32(HW_VDP_CTRL, VDP_CMD(VDP_CODE_VRAM_WR | VDP_CODE_DMA, 0x3000));
    m68k_wr16(HW_VDP_DATA, 0x7700);
    int ok = 1;
    for (int i = 2; i < 0x20; i++) ok &= nmd_vram()[0x3000 + i] == 0x77;
    CHECK(ok, "VRAM fill");
    CHECK(nmd_vram()[0x3022] == 0x00, "VRAM fill stops at length");

    /* Status: FIFO empty, VBlank while display is disabled. */
    m68k_wr16(HW_VDP_CTRL, VDP_REG(1, 0x04));
    uint16_t st = m68k_rd16(HW_VDP_CTRL);
    CHECK((st & VDP_STATUS_EMPTY) && (st & VDP_STATUS_VBLANK), "status %04X", st);
}

static void test_io(void)
{
    setup();
    CHECK((m68k_rd8(HW_IO_VERSION) & 0xC0) == 0x00, "version: domestic NTSC");
    m68k_wr8(HW_IO_CTRL1, 0x40);
    m68k_wr8(HW_IO_DATA1, 0x40);
    CHECK((m68k_rd8(HW_IO_DATA1) & 0x7F) == 0x7F, "pad idle, TH high");
    m68k_wr8(HW_IO_DATA1, 0x00);
    CHECK((m68k_rd8(HW_IO_DATA1) & 0x7F) == 0x33, "pad idle, TH low");

    /* Z80 RAM with BUSREQ. */
    m68k_wr16(HW_Z80_BUSREQ, 0x0100);
    CHECK((m68k_rd16(HW_Z80_BUSREQ) & 0x0100) == 0, "bus granted");
    m68k_wr8(0xA01FFF, 0x42);
    CHECK(m68k_rd8(0xA03FFF) == 0x42, "Z80 RAM mirror");
    CHECK(z80_rd(0x1FFF) == 0x42, "z80_rd");
}

static void test_trace(void)
{
    setup();
    char buf[4096] = {0};
    FILE *f = fmemopen(buf, sizeof buf, "w");
    nmd_trace_open(f);
    m68k_wr16(0xFF1234, 0x0A3C);
    (void)m68k_rd16(0xFF1234);
    m68k_wr8(0xFF0001, 0x7F);
    m68k_clr8(0xFF0001);
    nmd_trace_open(NULL);
    fclose(f);
    const char *want =
        "W16 FF1234 <- 0A3C\n"
        "R16 FF1234 -> 0A3C\n"
        "W8 FF0001 <- 7F\n"
        "R8 FF0001 -> 7F\n"
        "W8 FF0001 <- 00\n";
    CHECK(strcmp(buf, want) == 0, "trace text:\n%s", buf);

    nmd_trace_line l;
    CHECK(nmd_trace_parse("R16 FF1234 -> 0A3C", &l) == 1 && !l.write && l.bits == 16 &&
          l.addr == 0xFF1234 && l.value == 0x0A3C, "parse R16");
    CHECK(nmd_trace_parse("ZW8 4000 <- 2B\n", &l) == 1 && l.bus == NMD_TRACE_Z80 && l.write &&
          l.addr == 0x4000 && l.value == 0x2B, "parse ZW8");
    CHECK(nmd_trace_parse("# call 000200 Main", &l) == 0, "comment");
    CHECK(nmd_trace_parse("R16 FF1234 <- 0A3C", &l) == -1, "wrong arrow");
    CHECK(nmd_trace_parse("R16 FF123 -> 0A3C", &l) == -1, "short address");
    CHECK(nmd_trace_parse("R8 FF1234 -> 0A3C", &l) == -1, "wide 8-bit value");
}

static void test_state(void)
{
    setup();
    m68k_wr16(0xFF0010, 0xBEEF);
    nmd_cram()[5] = 0x0123;
    M68K.d[3] = 0x11223344;
    const char *path = "test_core_state.nmds";
    CHECK(nmd_state_save(path), "save");
    m68k_wr16(0xFF0010, 0);
    nmd_cram()[5] = 0;
    M68K.d[3] = 0;
    CHECK(nmd_state_load(path), "load");
    CHECK(m68k_rd16(0xFF0010) == 0xBEEF && nmd_cram()[5] == 0x0123 && M68K.d[3] == 0x11223344,
          "state round trip");
    remove(path);
}

static void test_replay(void)
{
    setup();
    /* A recorded trace: RAM accesses are ignored, device reads are replayed. */
    const char *rec =
        "R16 FF0000 -> 1234\n"
        "R16 C00004 -> BEEF\n"
        "W8 A10009 <- 40\n"
        "R8 A10003 -> 12\n";
    FILE *f = fmemopen((void *)rec, strlen(rec), "r");
    CHECK(nmd_replay_open(f), "replay open");
    fclose(f);
    CHECK(nmd_replay_remaining() == 2, "two device reads recorded");
    CHECK(m68k_rd16(0xFF0000) == 0, "RAM is not replayed");
    CHECK(m68k_rd16(HW_VDP_CTRL) == 0xBEEF, "VDP status replayed");
    CHECK(m68k_rd8(HW_IO_DATA1) == 0x12, "pad replayed");
    CHECK(nmd_replay_remaining() == 0, "replay consumed");
    nmd_set_fatal_handler(on_fatal);
    EXPECT_FATAL(m68k_rd16(HW_VDP_CTRL));                   /* nothing left */
    nmd_replay_close();

    f = fmemopen((void *)rec, strlen(rec), "r");
    nmd_replay_open(f);
    fclose(f);
    EXPECT_FATAL(m68k_rd8(HW_IO_DATA1));                    /* out of order */
    CHECK(strstr(fatal_msg, "recorded read16 C00004") != NULL, "mismatch message: %s", fatal_msg);
    nmd_replay_close();
    nmd_set_fatal_handler(NULL);
}

static void test_state_v2(void)
{
    setup();
    m68k_wr32(HW_VDP_CTRL, VDP_VRAM_WRITE(0x1234 & ~1u)); /* leaves addr/code latched */
    m68k_wr8(HW_IO_CTRL1, 0x40);
    m68k_wr16(HW_Z80_BUSREQ, 0x0100);
    const char *path = "test_core_v2.nmds";
    CHECK(nmd_state_save(path), "save");
    setup();
    CHECK(nmd_state_load(path), "load");
    m68k_wr16(HW_VDP_DATA, 0x5AA5);                         /* uses the restored latch */
    CHECK(nmd_vram()[0x1234] == 0x5A, "VDP address latch restored");
    CHECK(m68k_rd8(HW_IO_CTRL1) == 0x40, "I/O control restored");
    CHECK((m68k_rd16(HW_Z80_BUSREQ) & 0x0100) == 0, "Z80 bus request restored");
    remove(path);
}

int main(void)
{
    test_ram();
    test_fatal();
    test_vdp();
    test_io();
    test_trace();
    test_state();
    test_replay();
    test_state_v2();
    nmd_shutdown();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("test_core: ok\n");
    return 0;
}
