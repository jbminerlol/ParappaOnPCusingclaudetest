"""Finds Ghidra range checks that C evaluates differently from the MIPS code.

MIPS tests "x in [K, K+M)" as an unsigned compare: sltiu (x - K), M. Ghidra prints that as
"x - K < M"; when x is a narrow unsigned type (ushort, byte, ...) C promotes it to a signed int,
so x < K makes x - K negative and the test passes where the game's fails.

Usage: python tools/audit_narrow_compare.py <gen-dir> [--fix-list]
"""
import glob
import os
import re
import sys

NARROW = r'(?:ushort|byte|uchar|undefined2|undefined1|bool)'
NUM = r'(?:0x[0-9a-fA-F]+|\d+)'
CAST_EXPR = rf'\*\({NARROW} \*\)\((?:[^()]|\([^()]*\))*\)|\({NARROW}\)\w+'


def scan(path):
    text = open(path, encoding='utf-8').read()
    out = []
    for m in re.finditer(r'/\* ([0-9a-f]{8}) \*/(.*?)(?=/\* [0-9a-f]{8} \*/|\Z)', text, re.S):
        addr, body = m.group(1), m.group(2)
        narrow_vars = set(re.findall(rf'^\s*{NARROW} (\w+);', body, re.M))
        var_alt = '|'.join(sorted(map(re.escape, narrow_vars), key=len, reverse=True))
        left = CAST_EXPR + (f'|\\b(?:{var_alt})\\b' if var_alt else '')
        for c in re.finditer(rf'({left}) - ({NUM}) (<=?|>=?) ({NUM})', body):
            line = text.count('\n', 0, m.start(2) + c.start()) + 1
            out.append((addr, line, c.group(0)))
    return out


def main(gen):
    total = 0
    for path in sorted(glob.glob(os.path.join(gen, '*.c'))):
        for addr, line, expr in scan(path):
            total += 1
            print(f'{os.path.basename(path)}:{line} ({addr}): {expr}')
    print(f'{total} narrow unsigned range check(s)')


if __name__ == '__main__':
    main(sys.argv[1])
