"""Lists the library functions (and unnamed non-game functions) that game code calls.

Game code = main EXE range GAME_LO..GAME_HI plus every overlay function.
Usage: python tools/api_surface.py <analysis/decomp dir>
"""
import collections
import os
import re
import sys

GAME_LO, GAME_HI = 0x800127C4, 0x80028588
OVL_BASE = 0x801C3870
CHUNK = re.compile(r'^// ==== ([0-9a-f]+) (\S+) size=\d+\n(.*?)(?=^// ==== |\Z)', re.S | re.M)


def chunks(path):
    for m in CHUNK.finditer(open(path, encoding='utf-8').read()):
        yield int(m.group(1), 16), m.group(2), m.group(3)


def main(root):
    lib = {}
    for line in open(os.path.join(root, 'SCUS_941.83', 'functions.tsv')).read().splitlines()[1:]:
        a, n = line.split('\t')[:2]
        a = int(a, 16)
        if not (GAME_LO <= a < GAME_HI):
            lib[n] = a
    srcs = [os.path.join(root, 'SCUS_941.83', 'decomp.c')]
    srcs += [os.path.join(root, d, 'decomp.c') for d in sorted(os.listdir(root)) if d.startswith('COMOD')]
    uses = collections.Counter()
    for p in srcs:
        main_exe = 'SCUS' in p
        for a, n, body in chunks(p):
            if main_exe and not (GAME_LO <= a < GAME_HI):
                continue
            for c in set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', body)):
                if c in lib:
                    uses[c] += 1
    for n in sorted(uses, key=lambda n: lib[n]):
        print(f'{lib[n]:08x} {n:28s} {uses[n]}')
    print(len(uses), 'library functions called from game code', file=sys.stderr)


if __name__ == '__main__':
    main(sys.argv[1])
