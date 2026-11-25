# nativemd — plan update (2026-11-25, end of the session)

Supersedes the "Where we are" and "Next" of `overview.md` (same date). History:
`RECORD/2026-11-24.engine-bootstrap.completed.md`, `RECORD/2026-11-25.*.completed.md`.

## Where we are

| Milestone (2026-11-24 numbering) | State |
|---|---|
| 1 Contract + headers | draft 0.1; hardware points still "to verify"; one new open decision (ROM writes, below) |
| 2 Trace format + harness | done, plus suite coverage (`--coverage`) and argument limits that fail loudly |
| 3 Headless core | done; no engine change was needed by the client this session |
| 4 VDP rendering + test ROMs | not started |
| 5 Z80 scheduling | memory/arbitration done; coroutine not started |
| 6 PSG + YM2612 | not started |
| 7 `hello` end to end | done |
| 8 Sonic 1 client | in progress (private repository): 84 routines in C, the object loop runs from C, ~13.5 % of the game's 68000 instructions reachable from routines with C, all verified |
| 9 Desktop host | not started |
| 10 More games | not started |
| 11 Cartridge connector | API done; SRAM / mappers not started |

## Decisions in force

- Behaviour-exact, not cycle-exact; lag frames are outside the contract.
- Game C lives outside this repository; engine changes it needs land here first.
- Every routine with C: suites on captured, fuzzed and edge states, hybrid with C vs
  without C, and (new) a coverage report of the original code its states execute.
- Harness filters and limits must be proven not to hide data; limits fail loudly.

## Open decision

- **ROM writes.** Sonic 1's SolidObject (FixBugs = 0) writes through a1, which holds the
  object's code address when an object is off screen and pushed: a 16-bit write into ROM.
  Hardware is believed to ignore it (to verify on a MiSTer / real console); the engine
  treats it as fatal. Options: ignore ROM writes without a mapper (and trace them), or keep
  them fatal and let clients avoid them.

## Next

1. **Sonic 1 client**: the rest of Sonic's object, then the level loop and other zones.
2. **Harness**: check run time of large suites (parallel suites, or fewer synthetic states
   where coverage allows); a stack-filter test ROM (still open from `overview.md`).
3. **VDP rendering** (milestone 4), **hardware facts** (test ROMs, MiSTer), **Z80 coroutine**
   and **sound**, as in `overview.md`.

```mermaid
gantt
    title nativemd plan (update, 2026-11-25)
    dateFormat YYYY-MM-DD
    section Done
    Harness, oracle, hybrid, fuzzing, coverage  :done, d1, 2026-11-24, 2d
    Sonic 1: 84 routines, object loop in C      :done, d2, 2026-11-25, 1d
    section Sonic 1 client
    Rest of Sonic's object                      :s1, 2026-11-26, 10d
    Level loop, other zones                     :s2, after s1, 30d
    section Engine
    ROM-write decision                          :e0, 2026-11-26, 1d
    Suite run time, stack-filter test ROM       :e1, 2026-11-26, 3d
    VDP line renderer + frame dumps             :e2, after e1, 21d
    Hardware test ROMs (+ MiSTer runs)          :e3, after e1, 10d
    Z80 coroutine                               :e4, after e2, 10d
    PSG + YM2612                                :e5, after e4, 30d
```
