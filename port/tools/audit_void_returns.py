"""Finds game functions Ghidra typed as returning void although callers use their result.

Common pattern: a function ends with a call whose result it passes straight back (the callee's v0
is left untouched until jr ra). Ghidra infers "void", the decompiled callers lose the value and
loop/branch conditions come out wrong. A caller is flagged when, after "jal F" (and its delay
slot), it reads v0 before writing it or making another call.

Usage: python tools/audit_void_returns.py <analysis/decomp> <game dir: extracted/ + ghidra_in/>
"""
import csv
import os
import struct
import sys

OVL_BASE = 0x801C3870
GAME_LO, GAME_HI = 0x800127C4, 0x80028588


def load_funcs(path):
    with open(path, encoding='utf-8') as f:
        return [(int(r['address'], 16), int(r['size']), r['name'], r['signature'])
                for r in csv.DictReader(f, delimiter='\t')]


def regs(w):
    """(registers read, registers written) of a MIPS instruction (only what matters here)."""
    op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    if w == 0:
        return set(), set()
    if op == 0:
        f = w & 0x3F
        if f in (0, 2, 3):
            return {rt}, {rd}
        if f in (8,):
            return {rs}, set()
        if f == 9:
            return {rs}, {rd}
        if f in (16, 18):
            return set(), {rd}
        if f in (24, 25, 26, 27):
            return {rs, rt}, set()
        return {rs, rt}, {rd}
    if op in (2, 3):
        return set(), ({31} if op == 3 else set())
    if op in (4, 5):
        return {rs, rt}, set()
    if op in (1, 6, 7):
        return {rs}, set()
    if op == 15:
        return set(), {rt}
    if op >= 0x28:  # stores
        return {rs, rt}, set()
    if op >= 0x20:  # loads
        return {rs}, {rt}
    return {rs}, {rt}


def uses_v0_after(words, i):
    """words[i] is a jal; does code after its delay slot read v0 before redefining it?"""
    for k in range(i + 2, min(i + 40, len(words))):
        w = words[k]
        op = w >> 26
        r, wr = regs(w)
        if 2 in r:
            return True
        jump = op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and (w & 0x3F) in (8, 9))
        if jump:  # its delay slot runs first (before the call/branch target), then stop
            r2, _ = regs(words[k + 1]) if k + 1 < len(words) else (set(), set())
            return 2 in r2
        if 2 in wr:
            return False
    return False


def main(decomp, game):
    progs = [('main', os.path.join(game, 'extracted', 'SCUS_941.83'), False, 'SCUS_941.83')]
    for n in range(10):
        p = os.path.join(game, 'ghidra_in', f'COMOD{n}.BIN')
        if os.path.exists(p):
            progs.append((f'ov{n}', p, True, f'COMOD{n}.BIN'))
    void_main = {}
    found = {}
    for tag, img, ovl, ddir in progs:
        d = open(img, 'rb').read()
        base, data = (OVL_BASE, d) if ovl else (0x80010000, d[0x800:])
        funcs = load_funcs(os.path.join(decomp, ddir, 'functions.tsv'))
        void = {a: n for a, s, n, sig in funcs if sig.startswith('void ')
                and (GAME_LO <= a < GAME_HI or (ovl and a >= OVL_BASE))}
        if not ovl:
            void_main = void
        void_all = dict(void_main)
        void_all.update(void)
        for start, size, name, sig in funcs:
            if ovl and start < OVL_BASE:
                continue
            if not ovl and not (GAME_LO <= start < GAME_HI):
                continue
            words = [struct.unpack_from('<I', data, a - base)[0] for a in range(start, start + size, 4)]
            for i, w in enumerate(words):
                if w >> 26 != 3:
                    continue
                target = ((start + i * 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if target in void_all and uses_v0_after(words, i):
                    key = (tag if target >= OVL_BASE else 'main', target)
                    found.setdefault(key, []).append(f'{tag}:{start + i * 4:08x}')
    for (tag, target), callers in sorted(found.items()):
        print(f'{tag} {target:08x}: result used at {", ".join(callers[:6])}{" ..." if len(callers) > 6 else ""}')
    print(f'{len(found)} void-typed function(s) whose result is used')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
