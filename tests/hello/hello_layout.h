/* hello_layout.h - addresses shared by mkhello (ROM image) and the hello C. */
#ifndef HELLO_LAYOUT_H
#define HELLO_LAYOUT_H

#define HELLO_ROM_SIZE      0x1000u
#define HELLO_INITIAL_SSP   0x00FFFE00u

/* Code (hello.s; encoded by mkhello) */
#define HELLO_ADDR_MAIN     0x000200u
#define HELLO_ADDR_HINT     0x000400u
#define HELLO_ADDR_VINT     0x000420u /* ReadPad follows it */
#define HELLO_ADDR_ERROR    0x000480u
#define HELLO_CODE_END      0x000500u

/* Data */
#define HELLO_ADDR_PALETTE  0x000600u /* 16 words */
#define HELLO_ADDR_MESSAGE  0x000620u /* NUL-terminated ASCII */
#define HELLO_MESSAGE       "HELLO, WORLD"

/* Work RAM */
#define HELLO_RAM_FRAMES    0xFF0000u /* u16, incremented by VINT */
#define HELLO_RAM_VFLAG     0xFF0002u /* u8, set by main, cleared by VINT */
#define HELLO_RAM_PAD       0xFF0003u /* u8, SACBRLDU active high */
#define HELLO_RAM_HINTS     0xFF0004u /* u16, total HINTs taken          */

/* VRAM */
#define HELLO_PLANE_A       0xC000u
#define HELLO_TEXT_ROW      13
#define HELLO_TEXT_COL      14
#define HELLO_TEXT_VRAM     (HELLO_PLANE_A + (HELLO_TEXT_ROW * 64 + HELLO_TEXT_COL) * 2)

#endif
