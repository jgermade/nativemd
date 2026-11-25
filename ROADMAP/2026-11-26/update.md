# nativemd — plan update (2026-11-26)

Supersedes `ROADMAP/2026-11-25/update.md`. History: `RECORD/2026-11-25.rom-writes.completed.md`.

## Done since the last update

- ROM writes without a mapper: ignored as on hardware (to verify), always traced, reported
  at the end of a run (per address, with the origin of the first write), fatal with
  `--rom-writes fatal` / `NMD_ROM_WRITES_FATAL`.
- nmd-statefuzz says when its 16 `--rand` / `--value` limit is reached.
- The Sonic 1 client (private repository) now has Sonic's whole object in C (110 routines,
  ~17 % of the game's code reachable from them), verified; its check script runs in
  parallel (about 5 min).

## Next

1. **Suite size**: the client has suites near the 4096-state limit of `nmd_suite_main`;
   raise it (allocated list) keeping a loud failure for a real limit.
2. **Hardware facts**: test ROMs (`test-roms` repository) checked against reference
   emulators (Genesis Plus GX, BlastEm) that nativemd runs itself (`tools/refemu`); the four
   facts of contract section 3 agree on all machines. Next: VDP and timing facts as the
   renderer needs them; MiSTer / console runs of the same ROMs (green / red screen) for what
   the emulators cannot settle.
3. **VDP rendering** (milestone 4), **Z80 coroutine** (5), **sound** (6), as before.

```mermaid
gantt
    title nativemd plan (update, 2026-11-26)
    dateFormat YYYY-MM-DD
    section Done
    ROM writes: ignore, trace, report     :done, d1, 2026-11-26, 1d
    section Engine
    Suite size limit                       :e0, 2026-11-27, 1d
    Reference emulators, first test ROMs   :done, d2, 2026-11-26, 1d
    More test ROMs (+ MiSTer runs)         :e1, after e0, 10d
    VDP line renderer + frame dumps        :e2, after e0, 21d
    Z80 coroutine                          :e3, after e2, 10d
    PSG + YM2612                           :e4, after e3, 30d
```
