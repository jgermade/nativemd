# nativemd

A reusable **native Mega Drive / Genesis engine**: not an emulator, but a native
implementation of the console that the C of game decompilations links against. The game
provides its 68000 and Z80 programs as C; the engine provides the bus, VDP, DMA,
interrupts, sound, controllers, saves and frame pacing.

Fidelity: **behaviour-exact, not cycle-exact.** See [docs/contract.md](docs/contract.md).

Status: early draft (contract 0.1). Headless core, an oracle (Musashi running the original
68000 code on the same hardware model) and a `hello` program whose C produces exactly the
same bus trace as its 68000 code. Rendering, the Z80 scheduler and sound are not
implemented yet.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Layout

- `include/nativemd/` — the contract headers game C is written against.
- `src/` — the engine.
- `harness/oracle/` — `nmd-oracle`: original code on Musashi + the engine's hardware.
- `tools/` — `nmd-disasm` (68000 disassembler), `nmd-tracediff`, `nmd-statediff`,
  `mkhello` (test ROM generator).
- `tests/` — unit tests and `hello` (68000 source, C, generated ROM, golden files).
- `docs/` — contract, trace format, verification harness.
- `third_party/musashi/` — Musashi 68000 core (MIT), used only by the oracle and tools.
- [`ROADMAP/`](ROADMAP/) — the plan by date. [`RECORD/`](RECORD/) — project memory.
- [`AGENTS.md`](AGENTS.md) — rules for agents and humans.

No ROMs or game assets are distributed: every user supplies their own dump.

License: MIT (see [LICENSE](LICENSE)); third-party code keeps its own license.
