# Oracle and tools (2026-11-24, later the same day)

Decisions taken after the first plan of the day: MIT license; Musashi as the 68000
oracle, running the original code on the engine's own hardware model; vasm as the
assembler for test ROMs (not available in this environment yet). The oracle, a state diff
tool and a disassembler with coverage-guided code/data separation are done, and `hello`
passes the ASM-vs-C check.

What this changes in the plan:

- Milestone 2 (harness) moves ahead of the VDP renderer: the remaining harness pieces
  (state capture while playing, suite runner, side-effect replay, hybrid execution) are
  what Sonic 1 needs first.
- The decompilation toolchain is now part of the project: `nmd-disasm` grows into the
  ASM level (symbols, data/code split from oracle coverage, reassemblable output), and the
  oracle provides the coverage.

```mermaid
gantt
    title Harness and toolchain
    dateFormat YYYY-MM-DD
    section Done
    Oracle on Musashi + hello ASM vs C   :done, h0, 2026-11-24, 1d
    Disassembler with coverage           :done, d0, 2026-11-24, 1d
    section Harness
    State capture at breakpoints         :h1, 2026-11-25, 5d
    Per-routine suite runner             :h2, after h1, 5d
    Side-effect read replay              :h3, after h2, 4d
    Hybrid execution (oracle + C)        :h4, after h3, 10d
    section Toolchain
    vasm in the build (when reachable)   :t1, 2026-11-25, 2d
    Reassemblable disassembly output     :t2, after d0, 10d
    Z80 disassembly + oracle             :t3, after t2, 10d
    section Hardware
    VDP rendering                        :v1, 2026-11-25, 21d
```
