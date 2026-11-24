/*
 * suite.h - per-routine verification suite (docs/harness.md, "Suites").
 *
 * A game's test binary calls nmd_suite_main() with its routine table. For
 * each captured state it runs the routine on the oracle (original code) and
 * the C on the engine, then compares traces and final states.
 *
 * Usage: SUITE ROM --call ADDR|LABEL [--irq] [--sym FILE] [--regs LIST]
 *              [--ignore-ram LO-HI]... [--replay] [--keep DIR] [-v] STATE...
 *
 *   --irq         the routine is an interrupt handler (ends in RTE)
 *   --regs        output registers to compare, e.g. d0,a1,ccr
 *   --ignore-ram  RAM ranges not compared (the stack range is always skipped)
 *   --replay      feed the oracle's device reads to the C (side-effect replay)
 *   --keep DIR    write expected/actual traces of failing states there
 *
 * Exit status: 0 if every state passes, 1 otherwise, 2 on usage errors.
 */
#ifndef NMD_HARNESS_SUITE_H
#define NMD_HARNESS_SUITE_H

#include "nativemd/nativemd.h"

int nmd_suite_main(int argc, char **argv, const nmd_game *game);

#endif
