"""Mechanical MIPS R3000 -> C translation of a single function, for functions Ghidra cannot
decompile. The output is a fixed/<name>.c file: registers become locals, branches become gotos,
calls pass a0-a3 (plus stack words when the callee needs them) and the function returns v0.

Usage: python tools/mips2c.py <exe> <analysis/decomp> <start-hex> <end-hex> [<jr-hex>:<table-hex>:<n> ...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_decomp  # noqa: E402  (names of game/library functions)

REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
GP = 0x8006EA40
STACK_BYTES = 0x400


class Exe:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.base = struct.unpack_from('<I', d, 0x18)[0]
        self.data = d[0x800:]

    def word(self, addr):
        return struct.unpack_from('<I', self.data, addr - self.base)[0]


def r(n):
    return '0' if n == 0 else REG[n]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def mem(rs, off):
    return f'({r(rs)} + {off})'


class Translator:
    def __init__(self, exe, names, start, end, tables):
        self.exe, self.names, self.start, self.end = exe, names, start, end
        self.tables = tables  # jr address -> [targets]
        self.labels = set()
        for a in range(start, end, 4):
            w = exe.word(a)
            t = self.branch_target(a, w)
            if t is not None and start <= t < end:
                self.labels.add(t)
        for targets in tables.values():
            self.labels.update(targets)

    @staticmethod
    def branch_target(a, w):
        op = w >> 26
        if op in (4, 5, 6, 7) or (op == 1):
            return a + 4 + (s16(w & 0xFFFF) << 2)
        if op == 2:
            return (a & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        return None

    def simple(self, a, w):
        """C statement for a non-control-transfer instruction."""
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        sa, fn, imm = (w >> 6) & 31, w & 63, w & 0xFFFF
        si = s16(imm)
        if w == 0:
            return ''
        if op == 0:
            if rd == 0 and fn not in (0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13):
                return ''
            d = REG[rd]
            return {
                0x00: f'{d} = {r(rt)} << {sa};', 0x02: f'{d} = {r(rt)} >> {sa};',
                0x03: f'{d} = (uint32_t)((int32_t){r(rt)} >> {sa});',
                0x04: f'{d} = {r(rt)} << ({r(rs)} & 31);', 0x06: f'{d} = {r(rt)} >> ({r(rs)} & 31);',
                0x07: f'{d} = (uint32_t)((int32_t){r(rt)} >> ({r(rs)} & 31));',
                0x10: f'{d} = hi;', 0x12: f'{d} = lo;', 0x11: f'hi = {r(rs)};', 0x13: f'lo = {r(rs)};',
                0x18: f'{{ int64_t p = (int64_t)(int32_t){r(rs)} * (int32_t){r(rt)}; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }}',
                0x19: f'{{ uint64_t p = (uint64_t){r(rs)} * {r(rt)}; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }}',
                0x1A: f'if ({r(rt)}) {{ lo = (uint32_t)((int32_t){r(rs)} / (int32_t){r(rt)}); hi = (uint32_t)((int32_t){r(rs)} % (int32_t){r(rt)}); }}',
                0x1B: f'if ({r(rt)}) {{ lo = {r(rs)} / {r(rt)}; hi = {r(rs)} % {r(rt)}; }}',
                0x20: f'{d} = {r(rs)} + {r(rt)};', 0x21: f'{d} = {r(rs)} + {r(rt)};',
                0x22: f'{d} = {r(rs)} - {r(rt)};', 0x23: f'{d} = {r(rs)} - {r(rt)};',
                0x24: f'{d} = {r(rs)} & {r(rt)};', 0x25: f'{d} = {r(rs)} | {r(rt)};',
                0x26: f'{d} = {r(rs)} ^ {r(rt)};', 0x27: f'{d} = ~({r(rs)} | {r(rt)});',
                0x2A: f'{d} = (int32_t){r(rs)} < (int32_t){r(rt)};', 0x2B: f'{d} = {r(rs)} < {r(rt)};',
            }.get(fn) or self.fail(a, w)
        if rt == 0 and op not in (0x28, 0x29, 0x2A, 0x2B, 0x2E):
            return ''
        t = REG[rt]
        return {
            0x08: f'{t} = {r(rs)} + {si};', 0x09: f'{t} = {r(rs)} + {si};',
            0x0A: f'{t} = (int32_t){r(rs)} < {si};', 0x0B: f'{t} = {r(rs)} < (uint32_t){si};',
            0x0C: f'{t} = {r(rs)} & {imm:#x};', 0x0D: f'{t} = {r(rs)} | {imm:#x};',
            0x0E: f'{t} = {r(rs)} ^ {imm:#x};', 0x0F: f'{t} = {imm << 16:#x}u;',
            0x20: f'{t} = (uint32_t)*(int8_t *){mem(rs, si)};', 0x24: f'{t} = *(uint8_t *){mem(rs, si)};',
            0x21: f'{t} = (uint32_t)*(int16_t *){mem(rs, si)};', 0x25: f'{t} = *(uint16_t *){mem(rs, si)};',
            0x23: f'{t} = *(uint32_t *){mem(rs, si)};',
            0x28: f'*(uint8_t *){mem(rs, si)} = (uint8_t){r(rt)};',
            0x29: f'*(uint16_t *){mem(rs, si)} = (uint16_t){r(rt)};',
            0x2B: f'*(uint32_t *){mem(rs, si)} = {r(rt)};',
        }.get(op) or self.fail(a, w)

    def fail(self, a, w):
        raise SystemExit(f'mips2c: unsupported instruction {w:08x} at {a:08x}')

    def call(self, target):
        name = self.names.get(target)
        if name is None:
            return f'v0 = PSX_CALL({target:#010x}u)(a0, a1, a2, a3);'
        stack_args = ', '.join(f'*(uint32_t *)(sp + {0x10 + 4 * i})' for i in range(4))
        return f'v0 = {name}(a0, a1, a2, a3, {stack_args});'

    def translate(self, name):
        out = []
        a = self.start
        while a < self.end:
            w = self.exe.word(a)
            if a in self.labels:
                out.append(f'L{a:08x}:;')
            op, rs, rt, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 63
            target = self.branch_target(a, w)
            is_jump = op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and fn in (8, 9))
            if is_jump and a + 4 in self.labels:
                raise SystemExit(f'mips2c: branch target inside the delay slot at {a + 4:08x}')
            delay = self.simple(a + 4, self.exe.word(a + 4)) if is_jump else ''
            cond = None
            if op == 4:
                cond = f'{r(rs)} == {r(rt)}'
            elif op == 5:
                cond = f'{r(rs)} != {r(rt)}'
            elif op == 6:
                cond = f'(int32_t){r(rs)} <= 0'
            elif op == 7:
                cond = f'(int32_t){r(rs)} > 0'
            elif op == 1 and rt in (0, 1):
                cond = f'(int32_t){r(rs)} {"<" if rt == 0 else ">="} 0'
            if cond is not None:
                out.append(f'{{ int c = {cond}; {delay} if (c) goto L{target:08x}; }}')
                a += 8
                continue
            if op == 2:
                if self.start <= target < self.end:
                    out.append(f'{delay} goto L{target:08x};')
                else:
                    out.append(f'{delay} {self.call(target)} return v0;')
                a += 8
                continue
            if op == 3:
                out.append(f'{delay} {self.call((a & 0xF0000000) | ((w & 0x3FFFFFF) << 2))}')
                a += 8
                continue
            if op == 0 and fn == 8:
                if rs == 31:
                    out.append(f'{delay} return v0;')
                else:
                    cases = ' '.join(f'case {t:#010x}u: goto L{t:08x};' for t in self.tables[a])
                    out.append(f'{{ uint32_t j = {r(rs)}; {delay} switch (j) {{ {cases} default: psx_trap(-1); return v0; }} }}')
                a += 8
                continue
            if op == 0 and fn == 9:
                out.append(f'{{ uint32_t j = {r(rs)}; {delay} v0 = PSX_CALL(j)(a0, a1, a2, a3); }}')
                a += 8
                continue
            out.append(self.simple(a, w))
            a += 4
        body = '\n'.join('  ' + line for line in out if line.strip())
        regs = ', '.join(REG[i] + ' = 0' for i in range(1, 32) if REG[i] not in ('a0', 'a1', 'a2', 'a3', 'sp', 'gp'))
        return (f'/* {self.start:08x}: mechanically translated by tools/mips2c.py (Ghidra cannot decompile it). */\n'
                f'int {name}(p0, p1, p2, p3)\nuint32_t p0, p1, p2, p3;\n{{\n'
                f'  uint8_t stack[{STACK_BYTES:#x}];\n'
                f'  uint32_t a0 = p0, a1 = p1, a2 = p2, a3 = p3, hi = 0, lo = 0;\n'
                f'  uint32_t {regs};\n'
                f'  uint32_t sp = (uint32_t)(stack + sizeof(stack) - 0x20), gp = {GP:#x}u;\n'
                f'  (void)hi; (void)lo; (void)gp;\n'
                f'{body}\n  return v0;\n}}\n')


def names_for(decomp):
    g = gen_decomp.Generator(decomp, '', '')
    names = {}
    for name, (addr, _, final) in g.lib.items():
        names[addr] = 'psyq_' + final
    for addr, (name, _) in g.main.funcs.items():
        names[addr] = g.final_name(g.main, name)
    return names


def main():
    exe = Exe(sys.argv[1])
    start, end = int(sys.argv[3], 16), int(sys.argv[4], 16)
    tables = {}
    for spec in sys.argv[5:]:
        jr, table, n = spec.split(':')
        tables[int(jr, 16)] = [exe.word(int(table, 16) + 4 * i) for i in range(int(n))]
    names = names_for(sys.argv[2])
    name = names.get(start, f'FUN_{start:08x}')
    sys.stdout.write(Translator(exe, names, start, end, tables).translate(name))


if __name__ == '__main__':
    main()
