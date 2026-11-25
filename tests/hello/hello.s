; hello.s - the 68000 program of the hello test ROM (Motorola syntax).
;
; This is the reference source. No assembler is required to build the tests:
; tools/mkhello.c encodes these instructions directly, and the `hello_disasm`
; test checks the encoding by disassembling the ROM with Musashi and
; comparing with tests/hello/hello.lst. Assembling this file with vasm
; (vasmm68k_mot -Fbin) must give the same bytes in the code range.
;
; hello_game.c is the C version of this program, written against the
; nativemd contract. The `hello_oracle` test runs this code on the oracle and
; requires the same bus trace as the C.

FRAMES      equ $FF0000
VFLAG       equ $FF0002
PAD         equ $FF0003
HINTS       equ $FF0004
VDP_DATA    equ $C00000
VDP_CTRL    equ $C00004
IO_DATA1    equ $A10003
IO_CTRL1    equ $A10009
Z80_BUSREQ  equ $A11100
Z80_RESET   equ $A11200
PALETTE     equ $000600
MESSAGE     equ $000620

            org $000200
Main:
            clr.w   FRAMES
            clr.b   VFLAG
            clr.b   PAD
            clr.w   HINTS
            move.b  #$40,IO_CTRL1           ; TH as output

            ; Z80: take the bus, release reset, write "di; halt", give it back
            move.w  #$100,Z80_BUSREQ
            move.w  #$100,Z80_RESET
.wait_z80:  btst    #0,Z80_BUSREQ
            bne.s   .wait_z80
            move.b  #$F3,$A00000
            move.b  #$76,$A00001
            move.w  #0,Z80_RESET
            move.w  #0,Z80_BUSREQ
            move.w  #$100,Z80_RESET

            ; VDP registers
            move.w  VDP_CTRL,d0             ; reset the command latch
            move.w  #$8004,VDP_CTRL         ; HINT off (VInt turns it on)
            move.w  #$8114,VDP_CTRL         ; display off, DMA on
            move.w  #$8230,VDP_CTRL         ; plane A at $C000
            move.w  #$8407,VDP_CTRL         ; plane B at $E000
            move.w  #$8A1B,VDP_CTRL         ; HINT every 28 lines
            move.w  #$8C81,VDP_CTRL         ; H40
            move.w  #$8F02,VDP_CTRL         ; autoincrement 2
            move.w  #$9001,VDP_CTRL         ; 64x32 planes

            ; DMA the palette (16 words from PALETTE) to CRAM 0
            move.w  #$9310,VDP_CTRL
            move.w  #$9400,VDP_CTRL
            move.w  #$9500,VDP_CTRL         ; (PALETTE >> 1) & $FF
            move.w  #$9603,VDP_CTRL         ; (PALETTE >> 9) & $FF
            move.w  #$9700,VDP_CTRL
            move.l  #$C0000080,VDP_CTRL

            ; DMA fill plane A ($1000 bytes) with zero
            move.w  #$8F01,VDP_CTRL
            move.w  #$93FF,VDP_CTRL
            move.w  #$940F,VDP_CTRL
            move.w  #$9780,VDP_CTRL
            move.l  #$40000083,VDP_CTRL
            move.w  #0,VDP_DATA
            move.w  #$8F02,VDP_CTRL

            ; Print the message at row 13, column 14 of plane A
            move.l  #$469C0003,VDP_CTRL
            lea     MESSAGE,a0
.print:     moveq   #0,d0
            move.b  (a0)+,d0
            beq.s   .printed
            subi.w  #$20,d0                 ; tile = ASCII - 32
            move.w  d0,VDP_DATA
            bra.s   .print
.printed:
            move.w  #$8174,VDP_CTRL         ; display on, VINT on, DMA on
            move    #$2000,sr

MainLoop:   move.b  #1,VFLAG
WaitLoop:   tst.b   VFLAG                   ; idle loop (oracle --idle)
            bne.s   WaitLoop
WaitLoopEnd:
            bra.s   MainLoop

            org $000400
HInt:       addq.w  #1,HINTS
            rte

            org $000420
VInt:       movem.l d0-d1,-(sp)
            move.w  #$8014,VDP_CTRL         ; HINT on from the next frame
            addq.w  #1,FRAMES
            bsr.s   ReadPad
            move.b  d0,PAD
            clr.b   VFLAG
            movem.l (sp)+,d0-d1
            rte

            org $000460
ReadPad:    move.b  #$40,IO_DATA1
            move.b  IO_DATA1,d0             ; ?1CBRLDU
            andi.b  #$3F,d0
            move.b  #0,IO_DATA1
            move.b  IO_DATA1,d1             ; ?0SA00DU
            lsl.b   #2,d1
            andi.b  #$C0,d1
            or.b    d1,d0
            not.b   d0                      ; SACBRLDU, active high
            rts

            org $0004A0
Error:      bra.s   Error
