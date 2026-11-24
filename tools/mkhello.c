/*
 * mkhello - writes the "hello" test ROM image used by tests/hello.
 *
 * The image is ours (no third-party code or data): vectors, a valid header,
 * the 68000 program of tests/hello/hello.s and its data. The instructions
 * are encoded here directly so the build needs no assembler; the
 * `hello_disasm` test checks the encoding with Musashi's disassembler.
 *
 * Usage: mkhello OUT.bin [OUT.sym]
 *   OUT.sym lists "ADDRESS NAME" for the labels (used by nmd-oracle --idle).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hello_layout.h"

static uint8_t  rom[HELLO_ROM_SIZE];
static uint32_t pc;

static void put16(uint32_t at, uint16_t v) { rom[at] = (uint8_t)(v >> 8); rom[at + 1] = (uint8_t)v; }
static void put32(uint32_t at, uint32_t v) { put16(at, (uint16_t)(v >> 16)); put16(at + 2, (uint16_t)v); }

static void put_str(uint32_t at, size_t field, const char *s)
{
    memset(rom + at, ' ', field);
    memcpy(rom + at, s, strlen(s) < field ? strlen(s) : field);
}

/* ---- tiny encoder ---------------------------------------------------------- */

static void w(uint16_t v) { put16(pc, v); pc += 2; }
static void l(uint32_t v) { put32(pc, v); pc += 4; }

static void org(uint32_t at)
{
    if (at < pc) { fprintf(stderr, "mkhello: code overlaps at %06X (pc %06X)\n", at, pc); exit(1); }
    pc = at;
}

/* Labels and short-branch fixups. */
#define MAX_LABELS 32
static struct { const char *name; uint32_t addr; } labels[MAX_LABELS];
static size_t nlabels;
static struct { const char *name; uint32_t at; } fixups[MAX_LABELS];
static size_t nfixups;

static void label(const char *name) { labels[nlabels].name = name; labels[nlabels++].addr = pc; }

static uint32_t label_addr(const char *name)
{
    for (size_t i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0) return labels[i].addr;
    fprintf(stderr, "mkhello: undefined label %s\n", name);
    exit(1);
}

/* Bcc.s / BRA.s / BSR.s: opcode high byte + 8-bit displacement from pc+2. */
static void branch_s(uint8_t op, const char *target)
{
    fixups[nfixups].name = target;
    fixups[nfixups++].at = pc;
    w((uint16_t)(op << 8));
}

static void resolve_fixups(void)
{
    for (size_t i = 0; i < nfixups; i++) {
        int32_t disp = (int32_t)label_addr(fixups[i].name) - (int32_t)(fixups[i].at + 2);
        if (disp == 0 || disp < -128 || disp > 127) {
            fprintf(stderr, "mkhello: short branch to %s out of range (%d)\n", fixups[i].name, disp);
            exit(1);
        }
        rom[fixups[i].at + 1] = (uint8_t)(int8_t)disp;
    }
}

#define OP_BRA 0x60
#define OP_BSR 0x61
#define OP_BNE 0x66
#define OP_BEQ 0x67

static void clr_w_abs(uint32_t a)            { w(0x4279); l(a); }
static void clr_b_abs(uint32_t a)            { w(0x4239); l(a); }
static void move_b_imm_abs(uint8_t v, uint32_t a)  { w(0x13FC); w(v); l(a); }
static void move_w_imm_abs(uint16_t v, uint32_t a) { w(0x33FC); w(v); l(a); }
static void move_l_imm_abs(uint32_t v, uint32_t a) { w(0x23FC); l(v); l(a); }
static void btst_imm_abs(uint8_t bit, uint32_t a)  { w(0x0839); w(bit); l(a); }
static void move_w_abs_d0(uint32_t a)        { w(0x3039); l(a); }
static void move_b_abs_d0(uint32_t a)        { w(0x1039); l(a); }
static void move_b_abs_d1(uint32_t a)        { w(0x1239); l(a); }
static void move_w_d0_abs(uint32_t a)        { w(0x33C0); l(a); }
static void move_b_d0_abs(uint32_t a)        { w(0x13C0); l(a); }
static void lea_abs_a0(uint32_t a)           { w(0x41F9); l(a); }
static void tst_b_abs(uint32_t a)            { w(0x4A39); l(a); }
static void addq_w_1_abs(uint32_t a)         { w(0x5279); l(a); }

#define VDP_CTRL 0xC00004u
#define VDP_DATA 0xC00000u

static void emit_code(void)
{
    org(HELLO_ADDR_MAIN);
    label("Main");
    clr_w_abs(HELLO_RAM_FRAMES);
    clr_b_abs(HELLO_RAM_VFLAG);
    clr_b_abs(HELLO_RAM_PAD);
    clr_w_abs(HELLO_RAM_HINTS);
    move_b_imm_abs(0x40, 0xA10009);

    move_w_imm_abs(0x0100, 0xA11100);
    move_w_imm_abs(0x0100, 0xA11200);
    label("Main.wait_z80");
    btst_imm_abs(0, 0xA11100);
    branch_s(OP_BNE, "Main.wait_z80");
    move_b_imm_abs(0xF3, 0xA00000);
    move_b_imm_abs(0x76, 0xA00001);
    move_w_imm_abs(0x0000, 0xA11200);
    move_w_imm_abs(0x0000, 0xA11100);
    move_w_imm_abs(0x0100, 0xA11200);

    move_w_abs_d0(VDP_CTRL);
    static const uint16_t regs[] = { 0x8004, 0x8114, 0x8230, 0x8407, 0x8A1B, 0x8C81, 0x8F02, 0x9001 };
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) move_w_imm_abs(regs[i], VDP_CTRL);

    move_w_imm_abs(0x9310, VDP_CTRL);
    move_w_imm_abs(0x9400, VDP_CTRL);
    move_w_imm_abs((uint16_t)(0x9500 | ((HELLO_ADDR_PALETTE >> 1) & 0xFF)), VDP_CTRL);
    move_w_imm_abs((uint16_t)(0x9600 | ((HELLO_ADDR_PALETTE >> 9) & 0xFF)), VDP_CTRL);
    move_w_imm_abs((uint16_t)(0x9700 | ((HELLO_ADDR_PALETTE >> 17) & 0x7F)), VDP_CTRL);
    move_l_imm_abs(0xC0000080, VDP_CTRL);

    move_w_imm_abs(0x8F01, VDP_CTRL);
    move_w_imm_abs(0x93FF, VDP_CTRL);
    move_w_imm_abs(0x940F, VDP_CTRL);
    move_w_imm_abs(0x9780, VDP_CTRL);
    move_l_imm_abs(0x40000083, VDP_CTRL);
    move_w_imm_abs(0x0000, VDP_DATA);
    move_w_imm_abs(0x8F02, VDP_CTRL);

    move_l_imm_abs(0x40000000u | ((HELLO_TEXT_VRAM & 0x3FFFu) << 16) | (HELLO_TEXT_VRAM >> 14), VDP_CTRL);
    lea_abs_a0(HELLO_ADDR_MESSAGE);
    label("Main.print");
    w(0x7000);                       /* moveq #0,d0       */
    w(0x1018);                       /* move.b (a0)+,d0   */
    branch_s(OP_BEQ, "Main.printed");
    w(0x0440); w(0x0020);            /* subi.w #$20,d0    */
    move_w_d0_abs(VDP_DATA);
    branch_s(OP_BRA, "Main.print");
    label("Main.printed");
    move_w_imm_abs(0x8174, VDP_CTRL);
    w(0x46FC); w(0x2000);            /* move #$2000,sr    */

    label("MainLoop");
    move_b_imm_abs(0x01, HELLO_RAM_VFLAG);
    label("WaitLoop");
    tst_b_abs(HELLO_RAM_VFLAG);
    branch_s(OP_BNE, "WaitLoop");
    label("WaitLoopEnd");
    branch_s(OP_BRA, "MainLoop");

    org(HELLO_ADDR_HINT);
    label("HInt");
    addq_w_1_abs(HELLO_RAM_HINTS);
    w(0x4E73);                       /* rte */

    org(HELLO_ADDR_VINT);
    label("VInt");
    w(0x48E7); w(0xC000);            /* movem.l d0-d1,-(sp) */
    move_w_imm_abs(0x8014, VDP_CTRL);
    addq_w_1_abs(HELLO_RAM_FRAMES);
    branch_s(OP_BSR, "ReadPad");
    move_b_d0_abs(HELLO_RAM_PAD);
    clr_b_abs(HELLO_RAM_VFLAG);
    w(0x4CDF); w(0x0003);            /* movem.l (sp)+,d0-d1 */
    w(0x4E73);                       /* rte */

    label("ReadPad");
    move_b_imm_abs(0x40, 0xA10003);
    move_b_abs_d0(0xA10003);
    w(0x0200); w(0x003F);            /* andi.b #$3F,d0 */
    move_b_imm_abs(0x00, 0xA10003);
    move_b_abs_d1(0xA10003);
    w(0xE509);                       /* lsl.b #2,d1    */
    w(0x0201); w(0x00C0);            /* andi.b #$C0,d1 */
    w(0x8001);                       /* or.b d1,d0     */
    w(0x4600);                       /* not.b d0       */
    w(0x4E75);                       /* rts            */

    org(HELLO_ADDR_ERROR);
    label("Error");
    branch_s(OP_BRA, "Error_self");  /* bra.s * */
    labels[nlabels].name = "Error_self";
    labels[nlabels++].addr = HELLO_ADDR_ERROR;
    label("CodeEnd");
    if (pc > HELLO_CODE_END) { fprintf(stderr, "mkhello: code past HELLO_CODE_END\n"); exit(1); }
    resolve_fixups();
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s OUT.bin [OUT.sym]\n", argv[0]);
        return 2;
    }

    /* Vectors: everything not used goes to the error trap. */
    for (uint32_t v = 0; v < 0x100; v += 4) put32(v, HELLO_ADDR_ERROR);
    put32(0x000, HELLO_INITIAL_SSP);
    put32(0x004, HELLO_ADDR_MAIN);
    put32(0x070, HELLO_ADDR_HINT);
    put32(0x078, HELLO_ADDR_VINT);

    /* Header. */
    put_str(0x100, 16, "SEGA MEGA DRIVE");
    put_str(0x110, 16, "(C)NMD  2026.NOV");
    put_str(0x120, 48, "NATIVEMD HELLO");
    put_str(0x150, 48, "NATIVEMD HELLO");
    put_str(0x180, 14, "GM 00000000-00");
    put_str(0x190, 16, "J");
    put32(0x1A0, 0x000000);
    put32(0x1A4, HELLO_ROM_SIZE - 1);
    put32(0x1A8, 0xFF0000);
    put32(0x1AC, 0xFFFFFF);
    put_str(0x1B0, 12, "");
    put_str(0x1BC, 12, "");
    put_str(0x1C8, 40, "");
    put_str(0x1F0, 16, "JUE");

    emit_code();

    /* Data. */
    static const uint16_t palette[16] = {
        0x0000, 0x0EEE, 0x000E, 0x00E0, 0x0E00, 0x00EE, 0x0E0E, 0x0EE0,
        0x0222, 0x0444, 0x0666, 0x0888, 0x0AAA, 0x0CCC, 0x0246, 0x0642,
    };
    for (int i = 0; i < 16; i++) put16(HELLO_ADDR_PALETTE + 2u * (unsigned)i, palette[i]);
    memcpy(rom + HELLO_ADDR_MESSAGE, HELLO_MESSAGE, sizeof HELLO_MESSAGE);

    /* Checksum over $200..end. */
    uint16_t sum = 0;
    for (uint32_t i = 0x200; i < HELLO_ROM_SIZE; i += 2)
        sum = (uint16_t)(sum + (rom[i] << 8 | rom[i + 1]));
    put16(0x18E, sum);

    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(rom, 1, sizeof rom, f) != sizeof rom || fclose(f) != 0) {
        perror(argv[1]);
        return 1;
    }
    if (argc == 3) {
        FILE *s = fopen(argv[2], "w");
        if (!s) { perror(argv[2]); return 1; }
        for (size_t i = 0; i < nlabels; i++)
            if (strcmp(labels[i].name, "Error_self") != 0)
                fprintf(s, "%06X %s\n", labels[i].addr, labels[i].name);
        if (fclose(s) != 0) { perror(argv[2]); return 1; }
    }
    return 0;
}
