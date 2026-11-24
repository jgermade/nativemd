# nativemd contract (draft 0.1)

This is the contract between **game C code** (a decompilation of a Mega Drive game's 68000
and Z80 programs) and the **engine** (a native implementation of the rest of the console).
The headers in `include/nativemd/` are the normative form; this document explains them.

Status: **draft**. Contract 0.x may change. Contract 1.0 will be frozen, and every later 1.x
release stays backward compatible (old game C keeps building and behaving the same).

## 1. Fidelity promise

nativemd promises **behaviour-exact, not cycle-exact** execution:

- The sequence of data accesses the game makes to the bus, and their effects on hardware
  state (RAM, VRAM, CRAM, VSRAM, VDP registers, I/O, sound chip registers), is the same as
  on the original hardware for the same inputs.
- Instruction timing, bus contention, prefetch and DMA duration are **not** modelled.
  Anything in the original program that depends on them must be expressed explicitly in
  the C (see section 5, "Execution model").

## 2. Who provides what

| Game C provides | Engine provides |
|---|---|
| The 68000 program as C (`machine.h`) | Bus and memory map, RAM |
| The Z80 program as C (`z80.h`) | VDP (planes, sprites, DMA, interrupts) |
| A routine table: 68000 address → C function | Z80 RAM, bus arbitration, bank window |
| Optional cartridge chips (`cart.h`) | YM2612, SN76489 PSG |
| | Controllers, saves, frame pacing, host (`host.h`) |

The ROM image is supplied by the user at run time. Neither the engine nor a game's C
repository ships ROMs or game assets.

## 3. 68000 bus policy

- **All** data accesses go through `m68k_rd8/16/32` and `m68k_wr8/16/32`. Game C never keeps
  pointers into machine memory.
- Addresses are 24-bit; the top 8 bits are ignored.
- **16-bit accesses** are the native bus width. 8-bit accesses are separate bus cycles.
- **32-bit accesses** are two 16-bit accesses, high word first. `MOVE.L` to `-(An)` writes the
  low word first: use `m68k_wr32_predec`. *(To verify against the oracle per instruction.)*
- **Address errors**: a 16/32-bit access at an odd address is a fatal error. A game that
  deliberately relies on address-error exceptions must handle that case by hand in its C.
- **Prefetch** is not modelled. Self-modifying code that depends on prefetch must be
  rewritten by hand.
- **Instructions with special bus behaviour**:
  - `CLR` reads before it writes: `m68k_clr8/16/32`. (Musashi omits this read; the oracle
    adds it.)
  - `TAS` performs a locked read-modify-write; on the Mega Drive the write-back does not
    reach memory. `m68k_tas` performs only the read. *(To verify.)*
  - Other read-modify-write instructions to memory (`ADDQ`, `SUBQ`, `OR`, `AND`, `EOR`,
    `NOT`, `NEG`, `BSET`, ...): a read followed by a write, `M68K_RMW8/16/32`.
- **Reads with side effects** (VDP data and control ports, controller ports, Z80 bus
  status) are real reads: the C must make exactly the reads the original makes, in order,
  including reads whose value is discarded.
- **Unmapped addresses, writes to ROM without a mapper chip, Z80 space without BUSREQ**:
  fatal errors. The engine never silently ignores an access the game should not make.
- Byte writes to VDP ports put the byte on both halves of the 16-bit bus.
- Word accesses to 8-bit devices (Z80 space, I/O) perform one device access.
  *(To verify.)*

## 4. Registers and routines by address

- `M68K` holds `D0-D7`, `A0-A7` and `SR`. Most C keeps values in C locals; registers are used
  at the boundary of routines the original calls with arguments in registers, through
  `abi_` wrappers that read arguments from `M68K` and write results back.
- Every address reachable indirectly (exception vectors, jump tables, pointers in data) has
  an entry in the game's routine table (`nmd_game.routines`). `call_routine(addr)` and
  `jump_routine(addr)` dispatch through it.
- **A missing address is a fatal error, never a fallback.**
- The reset vector's PC must be in the table; so must the handler of every interrupt the
  game enables.

## 5. Execution model

- One host thread, deterministic.
- The game's reset routine runs in a coroutine. It runs until a **wait point**:
  `wait_vblank()` or `wait_until(cond, user)`.
- At a wait point the engine runs the rest of the frame:
  1. Active display: the line loop; the HINT handler runs on the lines where the HINT
     counter (register 10) expires, if enabled and not masked.
  2. VBlank: the VINT handler runs if enabled and not masked by `M68K.sr`.
  3. Chips' per-frame hooks, host presentation, controller sampling.
  4. The game resumes, inside VBlank.
- Interrupts are taken **only at wait points**. Interrupt handlers run with `M68K` saved and
  restored around them and the interrupt mask raised, like the 68000's exception entry
  and `RTE`.
- Busy-wait loops on state changed by interrupts or hardware (a flag cleared by VINT, VDP
  status polling) must be written as `wait_until`/`wait_vblank`. Their repeated reads are
  collapsed in traces (`docs/trace-format.md`, "Poll reads"). A loop that polls a
  register the engine resolves immediately (e.g. the Z80 BUSREQ grant) may stay a loop.
- Writes made by main code take effect immediately on engine state, but rendering of the
  whole frame happens at the wait point. Mid-frame effects must come from HINT handlers.
- Behaviour that depends on how long main code takes is outside the contract. Example: an
  interrupt that becomes pending while interrupts are masked during init, and fires when
  `SR` is lowered. A decompilation must reproduce the original's observable result by hand
  where that matters.
  *(Open: a `wait_line(n)` primitive for main-code raster effects.)*

### VDP timing

- DMA (68000 → VDP, fill, copy) completes immediately.
- The FIFO always reads empty; the DMA-busy bit always reads 0.
- The VBlank status bit reads 1 during VBlank (from the end of active display until the
  game resumes into the next frame's active display) and while the display is disabled.
- The H/V counter is coarse: it reflects only the scheduler phase.

## 6. Z80

The Z80 program is C against `z80.h`, with its own coroutine and clock (milestone 5; the
access functions exist now, the waits are fatal until then). The 68000 must hold BUSREQ to
touch Z80 space. The Z80's `8000-FFFF` window maps to the 68000 bus through the serial bank
register.

Sound verification compares the ordered sequence of writes to the YM2612 and PSG
(register and value), not audio samples.

## 7. Cartridge

`nmd_game` describes the game; the header of the user's ROM is parsed into
`nmd_cart_info`. Cartridge hardware (battery SRAM, mappers such as the SSF2 one, later
Sega CD / 32X) plugs in through `nmd_chip`: an address range on the 68000 bus with
read/write callbacks and reset/frame hooks.

## 8. Decisions still open

- Exact order of the two halves of `MOVE.L` per addressing mode, and of `MOVEM`.
- `TAS` write-back and word access to 8-bit devices: confirm on the oracle.
- Whether Z80 space is accessible while the Z80 is held in reset without BUSREQ.
- `wait_line(n)` for raster effects in main code.
- ~~Project license~~: MIT (decided 2026-11-24). The engine must not include code from
  GPL/AGPL references or from Genesis Plus GX (non-commercial licence).
