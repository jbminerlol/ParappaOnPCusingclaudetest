"""Flags call sites in the generated game code that pass fewer arguments than the callee declares
(Ghidra drops arguments set in branch delay slots or computed before a tail of other calls).

Usage: python tools/check_calls.py <analysis/decomp> <gen-dir>
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_decomp  # noqa: E402


def param_count(sig):
    params = sig[sig.index('(') + 1:sig.rindex(')')].strip()
    if params in ('', 'void'):
        return 0
    return len([p for p in params.split(',') if p.strip() != '...'])


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
                break
        if ch == ',' and depth == 1:
            args.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        args.append(cur)
    return args


def main(decomp, gen_dir):
    g = gen_decomp.Generator(decomp, '', '')
    want = {}
    for row in gen_decomp.load_tsv(os.path.join(decomp, 'SCUS_941.83', 'functions.tsv')):
        addr = int(row['address'], 16)
        name = row['name']
        if name in g.lib:
            want['psyq_' + g.lib[name][2]] = param_count(row['signature'])
    for prog in [g.main] + [p for _, p, _ in g.overlays]:
        for addr, (name, body) in prog.funcs.items():
            if 'DECOMPILE FAILED' in body:
                continue
            ret, _, params, _ = gen_decomp.split_definition(gen_decomp.strip_comments(body))
            want[g.final_name(prog, name)] = 0 if params in ('', 'void') else len(params.split(','))
    calls = []  # (file, line, caller, callee, nargs)
    for f in sorted(os.listdir(gen_dir)):
        if not f.endswith('.c') or f == 'dispatch.c':
            continue
        text = open(os.path.join(gen_dir, f), encoding='utf-8').read()
        current = None
        for m in re.finditer(r'^/\* ([0-9a-f]{8}) \*/\n\w[^\n]*?(\w+)\(|\b(\w+)\(', text, re.M):
            if m.group(2):
                current = m.group(2)
                continue
            callee = m.group(3)
            if callee not in want or callee == current:
                continue
            calls.append((f, text.count('\n', 0, m.start()) + 1, current, callee, len(split_args(text, m.end() - 1))))
    # A callee's arity is the larger of its signature and the most arguments any call site passes.
    most = {}
    for _, _, _, callee, n in calls:
        most[callee] = max(most.get(callee, 0), n)
    for f, line, caller, callee, n in calls:
        expect = max(want[callee], most[callee])
        if n < expect:
            print(f'{f}:{line}: {caller} calls {callee} with {n} args, expects {expect}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
