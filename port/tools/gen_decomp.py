"""Converts the Ghidra decompilation export (analysis/decomp) into compilable C for the port.

Output (all into <gen-dir>):
  game.h          declarations shared by every game translation unit
  game_main.c     game functions from the main EXE (0x800127C4..0x80028588)
  ovN.c           functions of stage overlay COMODN.BIN (all loaded at 0x801C3870)
  dispatch.c      MIPS address -> native function tables used for indirect calls

Rules:
  * Globals become address macros: DAT_8006edc0 -> (*(undefined4 *)0x8006edc0u).
  * Function definitions are rewritten K&R style and declared without prototypes, so call sites
    with Ghidra's occasionally inconsistent argument counts still compile (and match cdecl).
  * Library (PsyQ) functions are renamed psyq_<name>; the C++ runtime implements them.
  * Overlay functions are renamed ovN_<name>; overlays share addresses.
  * A function used as a value becomes its MIPS address; indirect calls go through PSX_CALL().
  * src/game/fixed/<name>.c (e.g. FUN_8001371c.c, ov6_FUN_801c740c.c) replaces a function's
    Ghidra output; it is #included in place and must use the final (renamed) identifiers.

Usage: python tools/gen_decomp.py <analysis/decomp> <fixed-dir> <gen-dir>
"""
import json
import os
import re
import sys

GAME_LO, GAME_HI = 0x800127C4, 0x80028588
OVL_BASE = 0x801C3870
FINGERPRINT_LEN = 64

CHUNK = re.compile(r'^// ==== ([0-9a-f]+) (\S+) size=\d+\n(.*?)(?=^// ==== |\Z)', re.S | re.M)
IDENT = re.compile(r'[A-Za-z_]\w*')
ADDR_SUFFIX = re.compile(r'^(?!(?:ov\d_)?FUN_|func_0x|LAB_)[A-Za-z_]\w*?_([0-9a-f]{8})$|^[a-z]+Ram([0-9a-f]{8})$')
PROTO = re.compile(r'^(?P<ret>[^\n(]*?[\s*])(?P<name>[A-Za-z_]\w*)\((?P<params>[^)]*)\)\s*$', re.M)

# Return types that Ghidra's PsyQ signatures get wrong (soft-float doubles come back in v0:v1).
RETURN_OVERRIDES = {'__adddf3': 'undefined8', '__divdf3': 'undefined8', '__muldf3': 'undefined8',
                    '__subdf3': 'undefined8', '__floatsidf': 'undefined8'}
RENAME_GAME = {'main': 'game_main'}
# Library functions the PsyQ signatures left unnamed or ambiguous (also applied by PsxFixups.java).
LIB_NAMES = {
    0x80040AE4: 'GsDefDispBuff', 0x80040C74: 'GsSetProjection', 0x80040CA4: 'GsDrawOt',
    0x80040F90: 'GsSetWorkBase', 0x8003B0CC: 'TransMatrix', 0x800392C0: 'StUnSetRing',
    0x80048A40: 'EnterCriticalSection', 0x80048A50: 'ExitCriticalSection', 0x8002AC20: 'SsInit',
}


def sanitize(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def load_tsv(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        header = f.readline().rstrip('\n').split('\t')
        for line in f:
            rows.append(dict(zip(header, line.rstrip('\n').split('\t'))))
    return rows


def chunks(path):
    for m in CHUNK.finditer(open(path, encoding='utf-8').read()):
        yield int(m.group(1), 16), m.group(2), m.group(3)


def strip_comments(body):
    return re.sub(r'/\*.*?\*/', '', body, flags=re.S)


def split_definition(body):
    """Returns (ret, name, params, rest) for the first function definition in a chunk."""
    m = PROTO.search(body)
    if not m:
        raise ValueError('no prototype in chunk:\n' + body[:300])
    return m.group('ret').strip(), m.group('name'), m.group('params').strip(), body[m.end():]


PARTIAL = re.compile(r'\b([A-Za-z_]\w*)\._(\d+)_(\d+)_')
PARTIAL_TYPES = {1: 'undefined1', 2: 'undefined2', 4: 'undefined4', 8: 'undefined8'}


def partial_access(m):
    """Ghidra's 'var._off_size_' (part of a variable) -> explicit typed access."""
    var, off, size = m.group(1), int(m.group(2)), int(m.group(3))
    return f'(*({PARTIAL_TYPES.get(size, "undefined4")} *)((undefined1 *)&{var} + {off}))'


STACK_VAR = r'(?:local_[0-9a-f]+|[a-z]*Stack_[0-9a-f]+)'
STACK_DECL = re.compile(r'^[ \t]+([A-Za-z_][\w ]*?)\s*(\**)(' + STACK_VAR + r')(?:\s*\[(\d+)\])?;[ \t]*\n', re.M)


def stack_frame(text):
    """Ghidra splits on-stack structs into separate locals (local_28, auStack_20, ...) whose names
    give their stack offsets. When any of them has its address taken or is an array, the original
    layout matters, so all of them become views into one frame buffer at their original offsets.
    Returns (text, macro definitions, macro names)."""
    # Declarations are the block between the opening brace and Ghidra's first blank line.
    start = text.find('{\n')
    if start < 0:
        return text, '', []
    start += 2
    blank = re.search(r'\n[ \t]*\n', text[start - 1:])
    end = start - 1 + blank.start() + 1 if blank else len(text)
    block = text[start:end]
    decls = list(STACK_DECL.finditer(block))
    if not decls:
        return text, '', []
    names = [m.group(3) for m in decls]
    sensitive = any(m.group(4) for m in decls) or any(re.search(r'&\s*' + n + r'\b', text) for n in names)
    if not sensitive:
        return text, '', []
    offset = {n: int(n.rsplit('_', 1)[1], 16) for n in names}
    base = (max(offset.values()) + 7) & ~7
    defs = []
    for m in decls:
        typ, stars, name, count = m.group(1).strip(), m.group(2), m.group(3), m.group(4)
        addr = f'(psx_frame + {base - offset[name]})'
        if count:
            defs.append(f'#define {name} (*({typ} {stars}(*)[{count}]){addr})')
        else:
            defs.append(f'#define {name} (*({typ} {stars}*){addr})')
    block = STACK_DECL.sub('', block)
    text = text[:start] + f'  undefined1 psx_frame[{base}] __attribute__((aligned(8)));\n' + block + text[end:]
    return text, ''.join(d + '\n' for d in defs), names


def game_ret(ret):
    """Ghidra sometimes types a function void although callers use its v0, so void becomes int."""
    return 'int' if ret == 'void' else ret



def lib_ret(ret):
    """Library return types as seen by game code: a full register (int), or v0:v1 for doubles.
    Ghidra's unmatched-library signatures say 'undefined' (1 byte), which would truncate."""
    return 'undefined8' if ret == 'undefined8' else 'int'


def knr(ret, name, params):
    """K&R definition header: 'int f(a, b)\\nint a;\\nshort b;'."""
    if params in ('', 'void'):
        return f'{ret} {name}()'
    names, decls = [], []
    for p in params.split(','):
        p = p.strip()
        if p == '...':
            continue
        pm = re.match(r'^(.*?)([A-Za-z_]\w*)(\s*\[\d*\])?$', p)
        names.append(pm.group(2))
        decls.append(p + ';')
    return f'{ret} {name}(' + ', '.join(names) + ')\n' + '\n'.join(decls)


def matching_paren(text, i):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '(':
            depth += 1
        elif text[j] == ')':
            depth -= 1
            if depth == 0:
                return j
    raise ValueError('unbalanced parens near: ' + text[i:i + 80])


def rewrite_indirect_calls(text):
    # (*(code *)EXPR)(...)  and  (**(code **)EXPR)(...)  ->  PSX_CALL(<pointer value>)(...)
    pat = re.compile(r'\((\*+)\(code (\*+)\)')
    while True:
        m = pat.search(text)
        if not m:
            break
        j = matching_paren(text, m.start())
        inner = text[m.start() + 1:j][1:]  # drop one dereference: we pass the pointer value
        text = text[:m.start()] + 'PSX_CALL(' + inner + ')' + text[j + 1:]
    # Locals declared 'code *pcVarN;' called as (*pcVarN)(...)
    for var in set(re.findall(r'\bcode \*(\w+);', text)):
        text = re.sub(r'\(\*' + re.escape(var) + r'\)\s*\(', f'PSX_CALL({var})(', text)
    return text


class Program:
    def __init__(self, tag, decomp_dir, game_range):
        self.tag = tag
        self.funcs = {}  # addr -> (name, body)
        lo, hi = game_range
        for addr, name, body in chunks(os.path.join(decomp_dir, 'decomp.c')):
            if lo <= addr < hi:
                self.funcs[addr] = (name, body)
        # Ghidra types untyped globals per function, so macros are emitted per function.
        self.func_globals = {}  # function addr -> {name: (address, type, size, widest access)}
        self.globals = {}       # name -> first seen info (fallback)
        for row in load_tsv(os.path.join(decomp_dir, 'globals.tsv')):
            info = (int(row['address'], 16), row['type'], int(row['size']), int(row.get('access') or row['size']))
            name = sanitize(row['name'])
            self.func_globals.setdefault(int(row['function'], 16), {})[name] = info
            self.globals.setdefault(name, info)


UNALIGNED_MASK = re.compile(r'(?<=[&|(,=] )(-1|0xffffffff[Uu]?) (<<|>>) (\([^()]*\) \* 8)')


NARROW_TYPE = r'(?:ushort|byte|uchar|undefined2|undefined1)'
NARROW_CAST = rf'\*\({NARROW_TYPE} \*\)\((?:[^()]|\([^()]*\))*\)|\({NARROW_TYPE}\)\w+'
NUM_LIT = r'(?:0x[0-9a-fA-F]+|\d+)'


def fix_narrow_range_checks(body, narrow_globals=()):
    """MIPS range checks "sltiu (x - K), M" come out as "x - K < M", or as "M < x - K" for the
    opposite test; with x a narrow unsigned type (a local, a cast or a global of this function) C
    promotes it to a signed int and x < K flips the result. Compare as unsigned instead."""
    names = set(re.findall(rf'^\s*{NARROW_TYPE} (\w+);', body, re.M)) | set(narrow_globals)
    left = NARROW_CAST
    if names:
        left += '|\\b(?:' + '|'.join(sorted(map(re.escape, names), key=len, reverse=True)) + ')\\b'
    body = re.sub(rf'({left}) - ({NUM_LIT}) (<=?|>=?) ({NUM_LIT})',
                  lambda m: f'(uint)({m.group(1)} - {m.group(2)}) {m.group(3)} {m.group(4)}', body)
    # Mirrored form, only as a whole operand: "(1 < mode - 1)", "&& 5 < x - 0x31)".
    return re.sub(rf'(?:(?<=\()|(?<=[,=&|?:] ))({NUM_LIT}) (<=?|>=?) ({left}) - ({NUM_LIT})(?=\s*(?:\)|;|&&|\|\||,|\?))',
                  lambda m: f'{m.group(1)} {m.group(2)} (uint)({m.group(3)} - {m.group(4)})', body)


class Generator:
    def __init__(self, decomp, fixed_dir, gen_dir):
        self.decomp, self.fixed_dir, self.gen_dir = decomp, fixed_dir, gen_dir
        # Text patches on Ghidra's output (src/game/patches.json, next to the fixed/ directory).
        self.patches = {}
        patches_path = os.path.join(os.path.dirname(os.path.abspath(fixed_dir)), 'patches.json')
        if fixed_dir and os.path.exists(patches_path):
            for p in json.load(open(patches_path, encoding='utf-8')):
                self.patches.setdefault(p['function'], []).append(p)
        main_dir = os.path.join(decomp, 'SCUS_941.83')
        self.lib = {}  # name -> (addr, ret)
        for row in load_tsv(os.path.join(main_dir, 'functions.tsv')):
            addr = int(row['address'], 16)
            if not (GAME_LO <= addr < GAME_HI):
                ret = row['signature'].split(row['name'] + '(')[0].strip()
                name = LIB_NAMES.get(addr, row['name'])
                self.lib[row['name']] = (addr, RETURN_OVERRIDES.get(name, ret), name)
        self.main = Program('main', main_dir, (GAME_LO, GAME_HI))
        self.overlays = []
        for d in sorted(os.listdir(decomp)):
            if d.startswith('COMOD'):
                n = int(re.search(r'COMOD(\d)', d).group(1))
                self.overlays.append((n, Program(f'ov{n}', os.path.join(decomp, d), (OVL_BASE, 0x80200000)),
                                      os.path.join(os.path.dirname(decomp), '..', 'game', 'ghidra_in', f'COMOD{n}.BIN')))
        self.main_names = {name: addr for addr, (name, _) in self.main.funcs.items()}
        self.lib_used = set()
        self.protos = {}  # final name -> return type

    def final_name(self, prog, name):
        if prog.tag != 'main' and name.startswith('FUN_801'):
            return f'{prog.tag}_{name}'
        return RENAME_GAME.get(name, name)

    def convert(self, prog):
        out_funcs = []
        own = {name: addr for addr, (name, _) in prog.funcs.items()}
        for addr in sorted(prog.funcs):
            name, body = prog.funcs[addr]
            used = {}
            fname = self.final_name(prog, name)
            fixed = os.path.join(self.fixed_dir, fname + '.c')
            if os.path.exists(fixed):
                text = open(fixed, encoding='utf-8').read()
                self.protos[fname] = game_ret(split_definition(strip_comments(text))[0])
                for g in IDENT.findall(strip_comments(text)):
                    self.note_global(prog, addr, g, used)
                # src/game is on the include path
                out_funcs.append(self.scoped(used, f'#include "fixed/{fname}.c"\n'))
                continue
            if 'DECOMPILE FAILED' in body:
                print(f'warning: {fname} failed to decompile; emitting a stub', file=sys.stderr)
                self.protos[fname] = 'int'
                out_funcs.append(f'int {fname}() {{ psx_unimplemented("{fname}"); return 0; }}\n')
                continue
            for p in self.patches.get(fname, []):
                if p['old'] not in body:
                    raise SystemExit(f'patch for {fname} does not apply: {p["old"]!r}')
                body = body.replace(p['old'], p['new'])
            # Ghidra keeps some characters of string contents in symbol names (s_~c888_>>>_800112d4).
            body = re.sub(r'\b(?:PTR_)?s_[^\s,;()\[\]{}]*?_[0-9a-f]{8}\b', lambda m: sanitize(m.group(0)), body)
            # Unaligned lwl/lwr/swl/swr come out as masks like "-1 << (u + 1) * 8" whose shift count
            # can reach 32, meaning an empty mask; x86 masks the count to 0 and keeps every bit.
            body = UNALIGNED_MASK.sub(
                lambda m: f'{"PSX_SHL" if m.group(2) == "<<" else "PSX_SHR"}({m.group(1)}, {m.group(3)})', body)
            body = fix_narrow_range_checks(body, [
                n for n, info in prog.func_globals.get(addr, {}).items()
                if re.fullmatch(NARROW_TYPE, info[1]) and info[2] == info[3]])
            # Upper bytes of a bool/char result ("CONCAT31(extraout_var, b)") are 0 on the MIPS (the
            # callee's sltu/andi sets the whole register); uninitialised in C they are stack garbage.
            body = re.sub(r'^(\s+[A-Za-z_][\w ]*?\**\s*)((?:extraout|in)_\w+);$', r'\1\2 = 0;', body, flags=re.M)
            ret, _, params, rest = split_definition(strip_comments(body))
            if ret == 'void':
                rest = re.sub(r'\breturn;', 'return 0;', rest)
            ret = game_ret(ret)
            self.protos[fname] = ret
            text = rewrite_indirect_calls(knr(ret, name, params) + rest)
            text = re.sub(r'\bfunc_0x([0-9a-f]{8})\s*\(', r'PSX_CALL(0x\1u)(', text)
            text = re.sub(r'&\s*(FUN_[0-9a-f]{8})\b', r'\1', text)
            text = re.sub(r'&\s*LAB_([0-9a-f]{8})\b', r'((undefined *)0x\1u)', text)
            text = PARTIAL.sub(partial_access, text)
            text = re.sub(r'\btrap\(', 'psx_trap(', text)
            # (*DAT_x)(...): the global holds a code pointer.
            text = re.sub(r'\(\*([A-Za-z_]\w*_[0-9a-f]{8})\)\s*\(', r'PSX_CALL(*(uint32_t *)&\1)(', text)
            text = IDENT.sub(lambda m: self.rewrite_ident(prog, addr, own, m, used), text)
            text, frame_defs, frame_names = stack_frame(text)
            body = f'/* {addr:08x} */\n' + frame_defs + text.strip() + '\n' + ''.join(f'#undef {n}\n' for n in frame_names)
            out_funcs.append(self.scoped(used, body))
        return out_funcs

    def scoped(self, used, text):
        """Wraps a function in the global macros it uses (with the types Ghidra gave them there)."""
        defs = ''.join(self.global_macro(n, used[n]) + '\n' for n in sorted(used))
        undefs = ''.join(f'#undef {n}\n' for n in sorted(used))
        return defs + text + undefs

    def rewrite_ident(self, prog, func, own, m, used):
        ident = m.group(0)
        text = m.string
        is_call = re.match(r'\s*\(', text[m.end():]) is not None
        if ident in own or ident in self.main_names:
            target = self.final_name(prog if ident in own else self.main, ident)
            if is_call:
                return target
            addr = own.get(ident, self.main_names.get(ident))
            return f'0x{addr:08x}u'
        if ident in self.lib and (is_call or ident not in prog.globals):
            self.lib_used.add(ident)
            if is_call:
                return 'psyq_' + self.lib[ident][2]
            return f'0x{self.lib[ident][0]:08x}u'
        am = ADDR_SUFFIX.match(ident) or re.match(r'^LAB_([0-9a-f]{8})$', ident)
        if am and ident.startswith('LAB_'):
            before = text[:m.start()].rstrip()
            if before.endswith('goto') or re.match(r'\s*:', text[m.end():]):
                return ident  # a real goto label
            addr = int(am.group(1), 16)
            return f'PSX_CALL(0x{addr:08x}u)' if is_call else f'0x{addr:08x}u'
        if am and is_call and ident not in prog.globals and ident not in self.main.globals:
            return f'PSX_CALL(0x{int(am.group(1) or am.group(2), 16):08x}u)'
        self.note_global(prog, func, ident, used)
        return ident

    def note_global(self, prog, func, ident, used):
        if ident in used:
            return
        info = prog.func_globals.get(func, {}).get(ident) or prog.globals.get(ident) or self.main.globals.get(ident)
        if info is None and ident.startswith('_'):
            # "_DAT_x": Ghidra accesses the global at its widest access size, not its declared type.
            base = (prog.func_globals.get(func, {}).get(ident[1:]) or prog.globals.get(ident[1:])
                    or self.main.globals.get(ident[1:]))
            if base:
                width = base[3] if base[3] in (1, 2, 4, 8) else 4
                info = (base[0], f'undefined{width}', width, width)
        if info is None:
            am = ADDR_SUFFIX.match(ident)
            if not am:
                return
            addr = int(am.group(1) or am.group(2), 16)
            # "iRam8006edbc": a global printed by type prefix + address; the export lists it under its
            # symbol name, so match by address (else infer the type from the prefix).
            for cand in prog.func_globals.get(func, {}).values():
                if cand[0] == addr:
                    width = cand[3] if cand[3] in (1, 2, 4, 8) else cand[2]
                    typ = cand[1] if cand[2] == width else f'undefined{width}'
                    info = (addr, typ, width, width)
                    break
            if info is None:
                prefix = ident[:ident.find('Ram')] if am.group(2) else ''
                typ = {'i': 'int', 's': 'short', 'us': 'ushort', 'c': 'char', 'b': 'byte', 'uc': 'uchar',
                       'u': 'undefined4', 'l': 'int', 'ul': 'uint'}.get(prefix)
                if typ is None:
                    typ = 'undefined4' if prefix.startswith('p') else 'undefined'
                size = {'short': 2, 'ushort': 2, 'char': 1, 'byte': 1, 'uchar': 1, 'undefined': 1}.get(typ, 4)
                info = (addr, typ, size, size)
        used[ident] = info

    @staticmethod
    def global_macro(name, info):
        addr, typ, size = info[:3]
        if typ in ('type', 'pointer', 'func *'):
            typ = 'undefined4' if typ != 'pointer' else 'pointer'
        typ = typ.replace('undefined3', 'undefined4')
        am = re.match(r'^(.*?)\[(\d+)\]$', typ)
        if am:
            return f'#define {name} (*({am.group(1)} (*)[{am.group(2)}])0x{addr:08x}u)'
        if typ in ('string', 'TerminatedCString') or (typ == 'char' and size > 1):
            return f'#define {name} (*(char (*)[{max(size, 1)}])0x{addr:08x}u)'
        return f'#define {name} (*({typ} *)0x{addr:08x}u)'

    def write_tu(self, path, funcs):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('/* Generated by tools/gen_decomp.py from the Ghidra export. Do not edit. */\n')
            f.write('#include "game.h"\n\n')
            f.write('\n'.join(funcs))

    def fingerprint(self, prog, path):
        """Picks the overlay's first function as a code fingerprint (code is never written)."""
        data = open(path, 'rb').read()
        first = min(prog.funcs)
        off = first - OVL_BASE
        return first, data[off:off + FINGERPRINT_LEN]

    def run(self):
        os.makedirs(self.gen_dir, exist_ok=True)
        self.write_tu(os.path.join(self.gen_dir, 'game_main.c'), self.convert(self.main))
        fps = []
        for n, prog, path in self.overlays:
            self.write_tu(os.path.join(self.gen_dir, f'{prog.tag}.c'), self.convert(prog))
            fps.append((n, prog) + self.fingerprint(prog, path))
        self.check_fingerprints(fps)
        self.write_header()
        self.write_dispatch(fps)

    def check_fingerprints(self, fps):
        images = {n: open(path, 'rb').read() for n, prog, path in self.overlays}
        for n, prog, addr, fp in fps:
            off = addr - OVL_BASE
            for m, img in images.items():
                if m != n and img[off:off + FINGERPRINT_LEN] == fp:
                    raise SystemExit(f'overlay fingerprint of COMOD{n} also matches COMOD{m}')

    def write_header(self):
        with open(os.path.join(self.gen_dir, 'game.h'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('/* Generated by tools/gen_decomp.py. Do not edit. */\n#pragma once\n#include "ghidra.h"\n\n')
            f.write('/* Game functions (declared without prototypes; see gen_decomp.py). */\n')
            for name in sorted(self.protos):
                f.write(f'{self.protos[name]} {name}();\n')
            f.write('\n/* PsyQ library functions called by game code; implemented natively in src/psyq. */\n')
            for name in sorted(self.lib_used):
                f.write(f'{lib_ret(self.lib[name][1])} psyq_{self.lib[name][2]}();\n')

    def write_dispatch(self, fps):
        with open(os.path.join(self.gen_dir, 'dispatch.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('/* Generated by tools/gen_decomp.py. Do not edit. */\n#include "game.h"\n#include "dispatch.h"\n\n')
            entries = [(addr, self.final_name(self.main, name)) for addr, (name, _) in self.main.funcs.items()]
            entries += [(self.lib[n][0], 'psyq_' + self.lib[n][2]) for n in self.lib_used]
            f.write('const PsxFnEntry psx_main_fns[] = {\n')
            for addr, name in sorted(entries):
                f.write(f'    {{0x{addr:08x}u, (psx_fn_t){name}}},\n')
            f.write('};\nconst int psx_main_fn_count = sizeof(psx_main_fns) / sizeof(psx_main_fns[0]);\n\n')
            for n, prog, addr, fp in fps:
                f.write(f'static const PsxFnEntry ov{n}_fns[] = {{\n')
                for a in sorted(prog.funcs):
                    f.write(f'    {{0x{a:08x}u, (psx_fn_t){self.final_name(prog, prog.funcs[a][0])}}},\n')
                f.write('};\n')
                f.write(f'static const unsigned char ov{n}_fp[] = {{' + ','.join(str(b) for b in fp) + '};\n')
            f.write('\nconst PsxOverlay psx_overlays[] = {\n')
            for n, prog, addr, fp in fps:
                f.write(f'    {{{n}, "COMOD{n}.BIN", 0x{addr:08x}u, ov{n}_fp, sizeof(ov{n}_fp), ov{n}_fns, '
                        f'sizeof(ov{n}_fns) / sizeof(ov{n}_fns[0])}},\n')
            f.write('};\nconst int psx_overlay_count = sizeof(psx_overlays) / sizeof(psx_overlays[0]);\n')


if __name__ == '__main__':
    Generator(*sys.argv[1:4]).run()
