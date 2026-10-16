"""Extracts the files the Ghidra analysis needs from your own PaRappa the Rapper disc image (US,
SCUS-94183) into game/ghidra_in:

  SCUS_941.83    the main executable (imported with the PSX loader)
  SCUS_text.bin  the same without its 0x800-byte PS-X EXE header (mapped under the overlays)
  COMODn.BIN     the stage overlays

Usage: python scripts/extract_disc.py <PARAPPA.CUE or .BIN> [out_dir]
The disc image is only read.
"""
import hashlib
import os
import re
import struct
import sys

SECTOR = 2352
SCUS_SHA1 = '22db3fce4ded847abc4bc85c0693687e1a99bae1'


def find_bin(path):
    if not path.lower().endswith('.cue'):
        return path
    text = open(path, encoding='latin-1').read()
    m = re.search(r'FILE\s+"([^"]+)"', text) or re.search(r'FILE\s+(\S+)', text)
    if m:
        cand = os.path.join(os.path.dirname(path), m.group(1))
        if os.path.exists(cand):
            return cand
    # A cue sheet whose FILE line names a renamed image: use the .bin with the cue's name.
    for ext in ('.bin', '.BIN'):
        cand = os.path.splitext(path)[0] + ext
        if os.path.exists(cand):
            return cand
    sys.exit(f'{path}: cannot find the .bin it points to')


class Disc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.f.seek(0, 2)
        if self.f.tell() % SECTOR:
            sys.exit(f'{path}: not a raw 2352-byte-sector image')

    def sector(self, lba):
        self.f.seek(lba * SECTOR)
        raw = self.f.read(SECTOR)
        return raw[24:24 + 2048]  # Mode 2 Form 1 user data

    def read(self, lba, size):
        out = bytearray()
        while len(out) < size:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:size])

    def walk(self, lba, size, prefix=''):
        data = self.read(lba, size)
        pos = 0
        while pos < len(data):
            n = data[pos]
            if n == 0:  # records never cross a sector: skip to the next one
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = data[pos:pos + n]
            ext, length, flags, nlen = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0], rec[25], rec[32]
            name = rec[33:33 + nlen].decode('latin-1')
            pos += n
            if name in ('\0', '\1'):
                continue
            if flags & 2:
                yield from self.walk(ext, length, prefix + name + '/')
            else:
                yield prefix + name.split(';')[0], ext, length


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'game', 'ghidra_in')
    disc = Disc(find_bin(sys.argv[1]))
    pvd = disc.sector(16)
    if pvd[1:6] != b'CD001':
        sys.exit('no ISO9660 volume on this disc')
    root_rec = pvd[156:156 + 34]
    files = list(disc.walk(struct.unpack_from('<I', root_rec, 2)[0], struct.unpack_from('<I', root_rec, 10)[0]))

    os.makedirs(out, exist_ok=True)
    wanted = [f for f in files if f[0].split('/')[-1] == 'SCUS_941.83' or re.fullmatch(r'COMOD\d\.BIN', f[0].split('/')[-1])]
    if not any(f[0].endswith('SCUS_941.83') for f in wanted):
        sys.exit('SCUS_941.83 not found: this is not the US PaRappa the Rapper disc')
    for path, lba, size in wanted:
        data = disc.read(lba, size)
        name = path.split('/')[-1]
        open(os.path.join(out, name), 'wb').write(data)
        print(f'{path:24} {size:8} bytes')
        if name == 'SCUS_941.83':
            if hashlib.sha1(data).hexdigest() != SCUS_SHA1:
                print('warning: SCUS_941.83 differs from the known US release; the port is made for SCUS-94183')
            if data[:8] != b'PS-X EXE':
                sys.exit('SCUS_941.83 is not a PS-X EXE')
            open(os.path.join(out, 'SCUS_text.bin'), 'wb').write(data[0x800:])
    print(f'-> {out}')


if __name__ == '__main__':
    main()
