# nativemd

A reusable **native Mega Drive / Genesis engine**: not an emulator, but a native
implementation of the console that the C of game decompilations links against. The game
provides its 68000 and Z80 programs as C; the engine provides the bus, VDP, DMA,
interrupts, sound, controllers, saves and frame pacing.

Fidelity: **behaviour-exact, not cycle-exact.** See [docs/contract.md](docs/contract.md).

Status: early draft (contract 0.1). Headless core, unit tests and a `hello` ROM run end
to end; rendering, the Z80 scheduler and sound are not implemented yet.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Layout

- `include/nativemd/` — the contract headers game C is written against.
- `src/` — the engine.
- `tools/` — `nmd-tracediff` (bus trace comparison), `mkhello` (test ROM generator).
- `tests/` — unit tests and the `hello` game (C + generated ROM + golden trace).
- `docs/` — contract, trace format, verification harness.
- [`ROADMAP/`](ROADMAP/) — the plan by date. [`RECORD/`](RECORD/) — project memory.
- [`AGENTS.md`](AGENTS.md) — rules for agents and humans.

No ROMs or game assets are distributed: every user supplies their own dump.
