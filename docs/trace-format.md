# Bus trace format

A trace is a text file with one bus **data** access per line, in the order the CPU made
them. Instruction fetches, prefetch and DMA reads by the VDP are **not** traced: they are
not part of the behaviour the C has to reproduce.

## Lines

```
R16 FF1234 -> 0A3C      68000 16-bit read: address, value read
W16 C00004 <- 8174      68000 16-bit write: address, value written
R8 A10003 -> 7F         68000 8-bit read
W8 FF0002 <- 01         68000 8-bit write
ZR8 4000 -> 00          Z80 read  (4-digit Z80 address)
ZW8 4001 <- 2B          Z80 write
# call 000200 Main      comment (ignored when comparing)
```

- Addresses: 6 hex digits for the 68000 (24-bit), 4 for the Z80. Values: 2 hex digits for
  8-bit accesses, 4 for 16-bit. Upper-case hex; lower case is accepted when parsing.
- 32-bit accesses appear as two 16-bit lines, in bus order.
- Read-modify-write and `CLR` appear as a read line followed by a write line.
- Comment lines start with `#`. The engine emits `call`/`ret`/`jump` around routine
  dispatches, `irq`/`rte` around interrupt handlers and `fatal` before aborting. They help
  humans; comparison ignores them, because the oracle does not produce the same ones.

## Comparing

`nmd-tracediff EXPECTED ACTUAL` compares two traces access by access and prints the first
divergence with context. Exit status: 0 identical, 1 different, 2 error.

The C library exposes `nmd_trace_parse` for other tools.

## Producing traces

- **Candidate** (game C on the engine): `nmd_trace_open(FILE *)`.
- **Oracle** (original ROM in an instrumented emulator): see `docs/harness.md`. The oracle's
  bus hook must filter out instruction fetches and prefetch.
