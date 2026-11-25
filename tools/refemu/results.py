#!/usr/bin/env python3
"""results.py - compare the result blocks of a test ROM run on several machines.

Usage: results.py NAME=FILE...
  FILE is a 64 KB RAM dump (refemu) or an nmd-oracle state file (.nmds). The test ROM's
  result block at $FF0000 ('NMDT', checks, mismatches, then observed / expected word
  pairs; test-roms/common/start.asm) is read from each.
Prints one line per check: the expected (contract) value and each machine's observed value,
marking disagreements. Exit status: 0 if every machine observed the expected values, 1 if
any check differs anywhere, 2 on errors.
"""
import struct, sys

RAM_OFF = 76        # RAM offset in a state file (docs/harness.md, "State files")

def ram(path):
    data = open(path, 'rb').read()
    if path.endswith('.nmds'):
        data = data[RAM_OFF:RAM_OFF + 0x10000]
    if len(data) < 0x10000:
        raise SystemExit('%s: not 64 KB of RAM' % path)
    return data

def block(path):
    r = ram(path)
    if r[:4] != b'NMDT':
        raise SystemExit('%s: no NMDT block (the test did not finish)' % path)
    n, bad = struct.unpack('>HH', r[4:8])
    pairs = [struct.unpack('>HH', r[8 + 4 * i:12 + 4 * i]) for i in range(n)]
    return pairs, bad

def main(args):
    if not args:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    runs = []
    for a in args:
        name, _, path = a.partition('=')
        runs.append((name, block(path)))
    counts = {len(p) for _, (p, _) in runs}
    if len(counts) != 1:
        print('different numbers of checks: ' + ', '.join('%s %d' % (n, len(p)) for n, (p, _) in runs))
        return 1
    status = 0
    print('check  expected  ' + '  '.join('%-8s' % n for n, _ in runs))
    for i in range(counts.pop()):
        exp = {p[i][1] for _, (p, _) in runs}
        e = runs[0][1][0][i][1]
        obs = [p[i][0] for _, (p, _) in runs]
        flag = '' if all(o == e for o in obs) and len(exp) == 1 else '   <-- differs'
        if flag:
            status = 1
        print('%5d  %04X      ' % (i, e) + '  '.join('%04X    ' % o for o in obs) + flag)
    return status

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
