# Musashi in nativemd

- Upstream: https://github.com/kstenerud/Musashi, commit
  `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd` (Musashi 4.10).
- License: MIT (see `readme.txt`, "LICENSE AND COPYRIGHT"). SoftFloat (`softfloat/`) is by
  John R. Hauser under its own permissive terms (see `softfloat/README.txt`, "Legal Notice").
- Used only by the harness (`nmd-oracle`) and tools (`nmd-disasm`). The engine library
  (`libnativemd`) does not link it.

## Local changes

- `m68kconf.h` is replaced by nativemd's configuration (the upstream file is kept as
  `m68kconf.orig.h`): 68000 only, no PMMU, separate immediate / PC-relative reads,
  `-(An)` long writes, and interrupt-acknowledge, RTE, TAS and instruction-hook callbacks.
- Not copied: `example/`, `test/`, `Makefile`.
- `m68kops.c` / `m68kops.h` are generated at build time by `m68kmake`.

No upstream source file is modified. Corrections to Musashi's behaviour (e.g. the read
cycle of `CLR`) are made in `harness/oracle/nmd_oracle.c`.
