/*
 * nmd-disasm - 68000 disassembler for Mega Drive ROMs (Musashi's disassembler).
 *
 * Usage: nmd-disasm ROM [--from HEX] [--to HEX] [--sym FILE] [--coverage FILE]
 *                       [--header] [--out FILE]
 *
 *   --from/--to   address range (default $200 to end of ROM)
 *   --sym         "ADDRESS NAME" lines; names are printed as labels, and
 *                 decoding never runs across one (they are instruction starts)
 *   --coverage    executed-instruction addresses (nmd-oracle --coverage):
 *                 only those are decoded as code, the rest is printed as data.
 *                 This is the first step of code/data separation.
 *   --header      print the vector table and the cartridge header first
 *
 * Output: one line per instruction, "ADDRESS  HEX-WORDS  INSTRUCTION".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "nativemd/cart.h"

static uint8_t *rom;
static size_t   rom_size;

static unsigned rd8(unsigned a)  { a &= 0xFFFFFF; return a < rom_size ? rom[a] : 0; }
static unsigned rd16(unsigned a) { return rd8(a) << 8 | rd8(a + 1); }

unsigned int m68k_read_disassembler_8(unsigned int a)  { return rd8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd16(a) << 16 | rd16(a + 2); }

typedef struct sym { uint32_t addr; char name[64]; } sym;
static sym   *syms;
static size_t nsyms;
static uint8_t *is_code; /* per even address, NULL = everything is code */

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

static void load_coverage(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    is_code = calloc(rom_size / 2 + 1, 1);
    unsigned a;
    while (fscanf(f, "%x", &a) == 1)
        if (a / 2 <= rom_size / 2) is_code[a / 2] = 1;
    fclose(f);
}

static void print_header(FILE *out)
{
    static const char *names[] = {
        "SSP", "Reset", "BusError", "AddressError", "Illegal", "ZeroDivide", "CHK", "TRAPV",
        "Privilege", "Trace", "LineA", "LineF",
    };
    fprintf(out, "; vectors\n");
    for (unsigned v = 0; v < 64; v++) {
        const char *n = v < 12 ? names[v] : v == 28 ? "HInt (level 4)" : v == 30 ? "VInt (level 6)" :
                        v >= 24 && v < 32 ? "Autovector" : v >= 32 && v < 48 ? "TRAP" : NULL;
        if (!n && v >= 12) continue;
        fprintf(out, ";   %03X %-16s %08X\n", v * 4, n, m68k_read_disassembler_32(v * 4));
    }
    nmd_cart_info ci;
    if (nmd_cart_parse(rom, rom_size, &ci)) {
        fprintf(out, "; header\n;   system    %s\n;   copyright %s\n;   name      %s\n"
                     ";   serial    %s\n;   checksum  %04X (computed %04X)\n;   ROM       %06X-%06X\n",
                ci.system, ci.copyright, ci.name_overseas, ci.serial, ci.checksum_header,
                ci.checksum_actual, ci.rom_start, ci.rom_end);
        if (ci.has_sram) fprintf(out, ";   SRAM      %06X-%06X\n", ci.sram_start, ci.sram_end);
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL, *out_path = NULL;
    unsigned from = 0x200, to = 0;
    bool header = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--from") && v)     { from = (unsigned)strtoul(v, NULL, 16); i++; }
        else if (!strcmp(a, "--to") && v)       { to = (unsigned)strtoul(v, NULL, 16); i++; }
        else if (!strcmp(a, "--sym") && v)      { load_syms(v); i++; }
        else if (!strcmp(a, "--coverage") && v) { if (!path) { fprintf(stderr, "ROM first\n"); return 2; } load_coverage(v); i++; }
        else if (!strcmp(a, "--out") && v)      { out_path = v; i++; }
        else if (!strcmp(a, "--header"))       header = true;
        else if (a[0] != '-' && !path) {
            path = a;
            FILE *f = fopen(path, "rb");
            if (!f) { perror(path); return 2; }
            fseek(f, 0, SEEK_END);
            rom_size = (size_t)ftell(f);
            fseek(f, 0, SEEK_SET);
            rom = malloc(rom_size);
            if (!rom || fread(rom, 1, rom_size, f) != rom_size) { perror(path); return 2; }
            fclose(f);
        }
        else { fprintf(stderr, "usage: nmd-disasm ROM [--from HEX] [--to HEX] [--sym FILE] "
                               "[--coverage FILE] [--header] [--out FILE]\n"); return 2; }
    }
    if (!path) { fprintf(stderr, "nmd-disasm: no ROM given\n"); return 2; }
    if (!to || to > rom_size) to = (unsigned)rom_size;
    FILE *out = out_path ? fopen(out_path, "w") : stdout;
    if (!out) { perror(out_path); return 2; }

    if (header) print_header(out);
    char text[128];
    for (unsigned pc = from & ~1u; pc < to;) {
        for (size_t i = 0; i < nsyms; i++)
            if (syms[i].addr == pc) fprintf(out, "%s:\n", syms[i].name);
        unsigned len = m68k_disassemble(text, pc, M68K_CPU_TYPE_68000);
        /* A symbol is a known instruction start: never decode across one. */
        bool crosses = false;
        for (size_t i = 0; i < nsyms; i++)
            if (syms[i].addr > pc && syms[i].addr < pc + len) crosses = true;
        if (crosses || (is_code && !is_code[pc / 2])) {
            fprintf(out, "%06X  %04X                     dc.w    $%04X\n", pc, rd16(pc), rd16(pc));
            pc += 2;
            continue;
        }
        char hex[32] = "";
        for (unsigned i = 0; i < len && i < 10; i += 2)
            snprintf(hex + strlen(hex), sizeof hex - strlen(hex), "%04X ", rd16(pc + i));
        fprintf(out, "%06X  %-25s%s\n", pc, hex, text);
        pc += len ? len : 2;
    }
    if (out != stdout) fclose(out);
    free(rom);
    return 0;
}
