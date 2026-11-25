#!/usr/bin/env python3
"""as_lst2sym.py - turn the symbol table of a Macro Assembler AS listing (-L) into
the "ADDRESS NAME" symbol files used by nmd-oracle, nmd-hybrid and nmd-disasm.

Usage: python3 tools/as_lst2sym.py sonic.lst sonic.sym
"""
import re, sys
src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding='latin-1').read().splitlines()
start = next(i for i, l in enumerate(lines) if 'Symbol Table' in l)
out = {}
for l in lines[start + 1:]:
    if not l.strip():
        if out: break
        continue
    for cell in l.split('|'):
        m = re.match(r'\s*\*?\s*([A-Za-z_.@$][\w.@$]*)\s*:\s*([0-9A-F]+)\s+C\s*$', cell)
        if m:
            out[m.group(1)] = int(m.group(2), 16) & 0xFFFFFF
with open(dst, 'w') as f:
    for name, addr in sorted(out.items(), key=lambda kv: kv[1]):
        f.write('%06X %s\n' % (addr, name))
print(len(out), 'symbols')
