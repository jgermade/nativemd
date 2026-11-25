# nativemd

A reusable **native Mega Drive / Genesis engine**: not an emulator, but a native
implementation of the console that the C of game decompilations links against. The game
provides its 68000 and Z80 programs as C; the engine provides the bus, VDP, DMA,
interrupts, sound, controllers, saves and frame pacing.

Fidelity: **behaviour-exact, not cycle-exact.** See [docs/contract.md](docs/contract.md).

Status: early draft (contract 0.1). Headless core and a verification harness: an oracle
(Musashi running the original 68000 code on the same hardware model), per-routine suites,
hybrid execution (original code and C mixed in one run), state fuzzing and per-suite
coverage of the original code. A `hello` program's C matches its 68000 code access for
access. The first real client, a Sonic 1 decompilation kept in a separate repository, has
84 routines in C (about 13.5 % of the game's 68000 instructions are reachable from them),
including the per-frame object loop, level collision, Sonic's collision with objects and
his ground movement. Each one is verified the same way: suites on real, fuzzed and edge
states, a coverage report, and hybrid runs that match runs without the C access for access.
Rendering, the Z80 scheduler and sound are not implemented yet. Current plan:
[ROADMAP/2026-11-25](ROADMAP/2026-11-25/update.md).

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Layout

- `include/nativemd/` — the contract headers game C is written against.
- `src/` — the engine.
- `harness/` — verification: the oracle (`nmd-oracle`, original code on Musashi + the
  engine's hardware), per-routine suites, side-effect replay, hybrid execution.
- `tools/` — `nmd-disasm` (68000 disassembler), `nmd-tracediff`, `nmd-statediff`,
  `mkhello` (test ROM generator).
- `tests/` — unit tests and `hello` (68000 source, C, generated ROM, golden files).
- `docs/` — contract, trace format, verification harness.
- `third_party/musashi/` — Musashi 68000 core (MIT), used only by the oracle and tools.
- [`ROADMAP/`](ROADMAP/) — the plan by date. [`RECORD/`](RECORD/) — project memory.
- [`AGENTS.md`](AGENTS.md) — rules for agents and humans.

No ROMs or game assets are distributed: every user supplies their own dump.

License: MIT (see [LICENSE](LICENSE)); third-party code keeps its own license.
