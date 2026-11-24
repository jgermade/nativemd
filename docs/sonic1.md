# Sonic 1 as the first client

How to rebuild Sonic the Hedgehog (REV01) from its disassembly and run it on nativemd.
No ROM, asset or disassembly is part of this repository: each user supplies their own
dump, and the disassembly is fetched from its own project.

## Tools

| Tool | Source | Notes |
|---|---|---|
| Disassembly | `sonicretro/s1disasm` (checked at commit `abcb582`) | `build.lua`, default `Revision = 1` |
| Assembler | `flamewing/asl-releases` (AS 1.42 build 313) | the official AS (build 317) does **not** build s1disasm |
| p2bin | `Clownacy/p2bin` @ `396d050f`, with submodules `accurate-kosinski` @ `45abe26a` (→ `clowncommon` @ `8e5b2254`) and `clownlzss` @ `283db3c2` (→ `clowncommon` @ `8edd4763`) | AS's own p2bin lacks `-z` (Kosinski) |
| Lua 5.4 | distribution package | runs `build.lua` |

Build AS with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASL_CPU_BACKENDS="M68K;Z80"`
and p2bin with `cmake -S . -B build -G Ninja`. Copy `asl` and `p2bin` over the ones in
`s1disasm/build_tools/Linux-x86_64/` and run `lua build_tools/chkbitperfect.lua`.

Result on 2026-11-24: `ROM is bit-perfect with REV01`, MD5 `09dadb5071eb35050067a32462e39c5f`,
identical to the user's dump.

## Symbols

`python3 tools/as_lst2sym.py sonic.lst s1.sym` (the build writes `sonic.lst`). Useful
addresses: `EntryPoint` $206, `VBlank` $B10, `HBlank` $1126, `WaitForVBlank` $29A0 (its
`.wait` loop is `$29A4-$29AA`), `v_gamemode` $FFF600, `v_vblank_routine` $FFF62A.

## Running it

```sh
nmd-oracle s1.bin --frames 300 --sym s1.sym --idle 29A4:29AA --trace o.trace --state-out o.nmds
nmd-hybrid s1.bin --frames 300 --sym s1.sym --idle 29A4:29AA --trace h.trace --state-out h.nmds
nmd-tracediff o.trace h.trace
```

Both reach the title screen (`v_gamemode = 04`) after 300 frames without touching unmodelled
hardware. The traces are identical for the first 343 441 data accesses; the first divergence
is the oracle's only lag frame (see below).

## Findings

- **VINT flag**: Sonic's VBlank reads the VDP status; bit 7 (VINT pending) must already be
  clear inside the handler (cleared by the interrupt acknowledge). The engine cleared it
  after the handler; fixed.
- **Lag frames**: when the main code runs past a VBlank with interrupts masked (e.g. loading
  art), the hardware takes the pending VINT as soon as `WaitForVBlank` lowers the mask, and
  Sonic runs its lag-frame VBlank routine (00). The engine's model has no elapsed time inside
  main code, so it never lags (by design, see the contract). The full-run oracle comparison
  therefore stops at the first lag frame; per-routine suites are unaffected, and full-run
  checks of C work best against hybrid runs (both on the engine's timing).

## First routines in C (2026-11-24)

`RandomNumber` ($29AC), `CalcSine` ($29D2) and `CalcAngle` ($2C6A), each with an ABI wrapper
reproducing the original register interface (which parts of d0/d1 change, preserved d3/d4,
CCR where it is cheap). The C lives outside this repository (it is derived from the game).

| Check | Result |
|---|---|
| Suites on captured states (1500 frames, demo) | 2 + 64 + 64 pass |
| Suites on fuzzed states | `RandomNumber` 500/500 (random seeds), `CalcSine` 256/256 (every angle), `CalcAngle` 2000/2000 random + 15/15 edge cases |
| Mutation checks | an equivalent mutation (`<` vs `<=` at equal magnitudes) survives, as it should; an edge-only bug at `-$8000` passes random states and fails edge cases |
| Hybrid with these 3 in C vs hybrid with none, 1500 frames | traces identical over 3 368 172 accesses (1015 calls into C), final states equal |
