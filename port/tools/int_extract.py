"""Extracts every file from PaRappa .INT archives.

INT layout: a series of sections, each a 0x2000-byte header followed by its files packed back to
back (padded to a 0x800 multiple). Header: u32 type (1 = TIM, 2 = VH/VB sound bank, 3 = TMD/TOD/
VDF/DAT/BEZ/TIM model+animation set), u32 file count, u32 data size in sectors, u32 0, then per
file: u32 size, char name[16]. A type of 0xFFFFFFFF ends the archive.

Usage: python tools/int_extract.py <out-dir> <file.INT>...
"""
import os
import struct
import sys


def sections(data):
    off = 0
    while off + 16 <= len(data):
        typ, n, sectors, _ = struct.unpack_from('<4I', data, off)
        if typ in (0, 0xFFFFFFFF) or n > 4096:
            return
        files, pos = [], off + 0x2000
        for i in range(n):
            p = off + 16 + i * 20
            size = struct.unpack_from('<I', data, p)[0]
            name = data[p + 4:p + 20].split(b'\0')[0].decode('latin1')
            files.append((name, data[pos:pos + size]))
            pos += size
        yield typ, files
        off += 0x2000 + sectors * 0x800


def main(out, paths):
    for path in paths:
        data = open(path, 'rb').read()
        base = os.path.join(out, os.path.basename(os.path.dirname(os.path.abspath(path))),
                            os.path.splitext(os.path.basename(path))[0])
        count = 0
        for idx, (typ, files) in enumerate(sections(data)):
            d = os.path.join(base, f'{idx}_type{typ}')
            os.makedirs(d, exist_ok=True)
            for name, blob in files:
                with open(os.path.join(d, name), 'wb') as f:
                    f.write(blob)
                count += 1
        print(f'{path}: {count} files -> {base}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
