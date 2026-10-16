"""Audits indirect calls: Ghidra decides the arguments of a call through a function pointer from
the pointer's (usually unknown) type, so it can drop arguments the MIPS code passes in a0-a3.
For each jalr in the game code this lists the argument registers written before it (since the
previous call or unconditional jump) next to the argument count of the matching PSX_CALL in the
generated C, and flags calls where the C passes fewer.

Usage: python tools/audit_indirect.py <analysis/decomp> <game dir with SCUS_941.83 + COMODn.BIN> <gen-dir>
"""
import csv
import os
import re
import struct
import sys

OVL_BASE = 0x801C3870
GAME_LO, GAME_HI = 0x800127C4, 0x80028588


def load_funcs(path):
    with open(path, encoding='utf-8') as f:
        return [(int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f, delimiter='\t')]


def image(path, overlay):
    d = open(path, 'rb').read()
    if overlay:
        return OVL_BASE, d
    return struct.unpack_from('<I', d, 0x18)[0], d[0x800:]


def mips_calls(base, data, start, size):
    """[(pc, highest arg register index written before the jalr, or -1)] in address order."""
    words = [struct.unpack_from('<I', data, a - base)[0] for a in range(start, start + size, 4)]
    out = []
    for i, w in enumerate(words):
        if (w >> 26) != 0 or (w & 0x3F) != 9:  # jalr
            continue
        written = set()
        # the delay slot runs before the call
        span = [i + 1] if i + 1 < len(words) else []
        j = i - 1
        while j >= 0 and i - j <= 24:
            ins = words[j]
            op, funct = ins >> 26, ins & 0x3F
            if op == 3 or (op == 0 and funct in (8, 9)) or op == 2:  # jal / jr / jalr / j
                if j + 1 in span and (op == 3 or (op == 0 and funct == 9)):
                    span.remove(j + 1)  # an earlier call's delay slot: clobbered by that call
                break
            span.append(j)
            j -= 1
        for k in span:
            ins = words[k]
            op = ins >> 26
            if op == 0:
                rd = (ins >> 11) & 31
            elif op in (2, 3) or (op >= 4 and op <= 7) or op == 1 or op >= 0x28:  # jumps, branches, stores
                continue
            else:
                rd = (ins >> 16) & 31
            if 4 <= rd <= 7:
                written.add(rd - 4)
        out.append((start + i * 4, max(written) if written else -1))
    return out


REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
IOPS = {8: 'addi', 9: 'addiu', 10: 'slti', 11: 'sltiu', 12: 'andi', 13: 'ori', 15: 'lui', 32: 'lb', 33: 'lh',
        35: 'lw', 36: 'lbu', 37: 'lhu', 40: 'sb', 41: 'sh', 43: 'sw', 4: 'beq', 5: 'bne', 6: 'blez', 7: 'bgtz'}
RFUN = {0: 'sll', 2: 'srl', 3: 'sra', 8: 'jr', 9: 'jalr', 16: 'mfhi', 18: 'mflo', 24: 'mult', 26: 'div',
        33: 'addu', 35: 'subu', 36: 'and', 37: 'or', 38: 'xor', 39: 'nor', 42: 'slt', 43: 'sltu'}


def disasm(w, pc):
    op, rs, rt, rd = w >> 26, REG[(w >> 21) & 31], REG[(w >> 16) & 31], REG[(w >> 11) & 31]
    imm = w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if w == 0:
        return 'nop'
    if op == 0:
        f = w & 0x3F
        name = RFUN.get(f, f'special{f}')
        if f in (0, 2, 3):
            return f'{name} {rd}, {rt}, {(w >> 6) & 31}'
        if f in (8, 9):
            return f'{name} {rs}'
        return f'{name} {rd}, {rs}, {rt}'
    if op in (2, 3):
        return f'{"j" if op == 2 else "jal"} {(pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2):08x}'
    name = IOPS.get(op, f'op{op}')
    if op >= 32:
        return f'{name} {rt}, {simm}({rs})'
    if op == 15:
        return f'lui {rt}, 0x{imm:04x}'
    if op in (4, 5, 6, 7):
        return f'{name} {rs}, {rt}, {pc + 4 + simm * 4:08x}'
    return f'{name} {rt}, {rs}, {simm}'


def split_args(text, i):
    """Arguments of the call whose '(' is at text[i]."""
    depth, args, cur = 0, [], ''
    for ch in text[i:]:
        if ch == '(':
            depth += 1
            if depth == 1:
                continue
        elif ch == ')':
            depth -= 1
            if depth == 0:
                if cur.strip():
                    args.append(cur)
                return args
        elif ch == ',' and depth == 1:
            args.append(cur)
            cur = ''
            continue
        cur += ch
    return args


def c_calls(gen_file):
    """{function address: [arg count of each PSX_CALL in source order]}"""
    text = open(gen_file, encoding='utf-8').read()
    res, cur = {}, None
    for m in re.finditer(r'/\* ([0-9a-f]{8}) \*/|PSX_CALL\(', text):
        if m.group(1):
            cur = int(m.group(1), 16)
            continue
        # skip "PSX_CALL(target)" to the argument list
        depth, i = 0, m.end() - 1
        while True:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        res.setdefault(cur, []).append(len(split_args(text, i + 1)))
    return res


def main(decomp, game, gen):
    progs = [('main', os.path.join(game, '..', 'extracted', 'SCUS_941.83'), False, os.path.join(decomp, 'SCUS_941.83'), 'game_main.c')]
    for n in range(10):
        p = os.path.join(game, f'COMOD{n}.BIN')
        if os.path.exists(p):
            progs.append((f'ov{n}', p, True, os.path.join(decomp, f'COMOD{n}.BIN'), f'ov{n}.c'))
    bad = 0
    for tag, img, ovl, ddir, genf in progs:
        base, data = image(img, ovl)
        calls = c_calls(os.path.join(gen, genf))
        for start, size in load_funcs(os.path.join(ddir, 'functions.tsv')):
            if ovl and start < OVL_BASE:
                continue
            if not ovl and not (GAME_LO <= start < GAME_HI):
                continue
            mc = mips_calls(base, data, start, size)
            cc = calls.get(start, [])
            if not mc and not cc:
                continue
            if len(mc) != len(cc):
                print(f'{tag} {start:08x}: {len(mc)} jalr vs {len(cc)} PSX_CALL (check by hand)')
            for k, (pc, hi) in enumerate(mc):
                n = cc[k] if k < len(cc) else None
                if n is not None and hi + 1 > n:
                    bad += 1
                    print(f'{tag} {start:08x}: jalr at {pc:08x} sets a0..a{hi}, C passes {n}')
                    if '-v' in sys.argv:
                        for a in range(pc - 40, pc + 8, 4):
                            print(f'      {a:08x}  {disasm(struct.unpack_from("<I", data, a - base)[0], a)}')
    print(f'{bad} call(s) with missing arguments')


if __name__ == '__main__':
    main(*sys.argv[1:4])
