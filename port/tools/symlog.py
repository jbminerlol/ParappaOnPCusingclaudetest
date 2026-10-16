"""Symbolizes the stack traces in parappa.log (crashes, redirected NULL accesses, watchpoint hits,
stall reports) with llvm-addr2line.

Usage: python tools/symlog.py [parappa.log] [parappa.exe]
"""
import glob
import os
import re
import subprocess
import sys

TOOLS = os.environ.get('PARAPPA_TOOLS') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools')
ADDR2LINE = (os.environ.get('ADDR2LINE') or
             next(iter(sorted(glob.glob(os.path.join(TOOLS, 'llvm-mingw-*', 'bin', 'llvm-addr2line.exe')), reverse=True)), 'llvm-addr2line'))
HEAD = re.compile(r'(Crash: .*?|Low-address access .*?|Watch: .*?|Main thread) at ([0-9A-F]{8}) \(exe base ([0-9A-F]{8})\)'
                  r'|(Watch: .*?) by ([0-9A-F]{8}) \(exe base ([0-9A-F]{8})\)')
FRAME = re.compile(r'#\d+ ([0-9A-F]{8})')


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ['APPDATA'], 'PaRappaPort', 'parappa.log')
    build = os.environ.get('PARAPPA_BUILD') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build')
    exe = sys.argv[2] if len(sys.argv) > 2 else os.path.join(build, 'parappa.exe')
    events = []
    for line in open(log, encoding='utf-8', errors='replace'):
        m = HEAD.search(line)
        if m:
            what = m.group(1) or m.group(4)
            pc, base = (m.group(2), m.group(3)) if m.group(1) else (m.group(5), m.group(6))
            events.append([what.strip(), int(base, 16), [int(pc, 16)]])
            continue
        f = FRAME.search(line)
        if f and events and len(events[-1][2]) < 12:
            events[-1][2].append(int(f.group(1), 16))
    for what, base, pcs in events:
        adj = ['0x%X' % (p - base + 0x400000) for p in pcs if base <= p < base + 0x2000000]
        out = subprocess.run([ADDR2LINE, '-f', '-C', '-s', '-e', exe] + adj, capture_output=True, text=True).stdout.split('\n')
        names = [f'{out[i]} ({out[i + 1]})' for i in range(0, len(out) - 1, 2)]
        print(what)
        for n in names:
            if not n.startswith('??'):
                print('   ', n)


if __name__ == '__main__':
    main()
