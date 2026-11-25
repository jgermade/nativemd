# nativemd — state at the end of the 2026-11-26 session (handoff)

Complements `update.md` (same date), which stays the plan. Records of the session:
`RECORD/2026-11-25.rom-writes.completed.md`, `RECORD/2026-11-26.reference-emulators.completed.md`.

## Done in this session

- ROM writes without a mapper: ignored, traced, reported; `--rom-writes fatal` strict mode.
- Suites: no fixed 4096-state limit; nmd-statefuzz says when its 16-field limit is hit.
- Reference emulators (`tools/refemu`): Genesis Plus GX and BlastEm as libretro cores run
  by our own runner; the four *to verify* facts of contract section 3 agree on nativemd and
  both emulators (test ROMs in the client repository).

## Branch and merge state

- Work branch: `ccr-4dd90776-1t6sym`, not merged, no PR opened yet. The user merges with
  **squash**.
- The client (nativemd-internal) pins this repository as a submodule at a commit of that
  branch. A squash merge discards that commit, so after merging here the client's
  submodule must be moved to the squash commit on `main` *before* the client's own merge.

## Resuming in a new session / branch

1. Start the new branch from `main` after the merges (or from `ccr-4dd90776-1t6sym` if it
   was not merged yet).
2. `cmake -S . -B build && cmake --build build && (cd build && ctest)`: 22 tests.
3. Reference emulators: `tools/refemu/fetch_cores.sh` (needs github.com), then
   `tools/refemu/run.sh DIR` with a test ROM directory (the client's `test-roms/run.sh`
   does both and builds nmd-oracle).
4. `work/` and `build/` are not committed: a fresh container rebuilds them.

## Next (from update.md, in order)

1. Hardware facts as the VDP renderer needs them: new test ROMs, checked against the
   reference emulators; MiSTer / console runs for confirmation.
2. VDP rendering (milestone 4), Z80 coroutine (5), PSG + YM2612 (6).
