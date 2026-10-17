#!/usr/bin/env python3
"""Сверка addr.h с эталонным bugs.exe: байты BB_SITE, слоты BB_IAT, CRC32 .text и маркер версии.
Запуск: python3 -I tools/verify_exe.py [путь к bugs.exe] (по умолчанию ../orig/bugs.exe)."""
import pathlib
import re
import struct
import sys
import zlib

here = pathlib.Path(__file__).resolve().parent.parent
exe = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent / 'orig' / 'bugs.exe'
addr_h = (here / 'src' / 'addr.h').read_text(encoding='utf-8')
d = exe.read_bytes()

pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + optsz + 40 * i + 8) for i in range(nsec)]


def off(va):
    for vs, rva, rs, ra in secs:
        if base + rva <= va < base + rva + max(vs, rs):
            return ra + va - base - rva
    raise ValueError(hex(va))


def const(name):
    m = re.search(r'#define\s+' + name + r'\s+(0x[0-9a-fA-F]+)', addr_h)
    return int(m.group(1), 16)


bad = 0
imports = {}
irva = struct.unpack_from('<I', d, pe + 24 + 104)[0]
o = off(base + irva)
while True:
    oft, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
    o += 20
    if not name:
        break
    dll = d[off(base + name):d.index(b'\0', off(base + name))].decode()
    k = 0
    while True:
        e = struct.unpack_from('<I', d, off(base + (oft or ft)) + 4 * k)[0]
        if not e:
            break
        if not e & 0x80000000:
            fn = d[off(base + e) + 2:d.index(b'\0', off(base + e) + 2)].decode()
            imports[(dll.lower(), fn)] = base + ft + 4 * k
        k += 1

if base != const('BB_IMAGE_BASE'):
    print(f'FAIL base {base:#x}'); bad += 1
ver = struct.unpack_from('<Q', d, off(const('BB_VERSION_ADDR')))[0]
print(('ok  ' if ver == const('BB_VERSION_1_6') else 'FAIL') + f' version qword {ver:#x}')
bad += ver != const('BB_VERSION_1_6')
t0 = off(const('BB_TEXT_VA'))
crc = zlib.crc32(d[t0:t0 + const('BB_TEXT_SIZE')])
print(('ok  ' if crc == const('BB_TEXT_CRC32') else 'FAIL') + f' CRC32 .text {crc:#010x}')
bad += crc != const('BB_TEXT_CRC32')

for name, va, hexs in re.findall(r'X\((\w+),\s*(0x[0-9a-fA-F]+),\s*"([0-9a-fA-F ]+)"\)', addr_h):
    want = bytes.fromhex(hexs)
    have = d[off(int(va, 16)):off(int(va, 16)) + len(want)]
    ok = have == want
    bad += not ok
    print(('ok  ' if ok else 'FAIL') + f' site {name} {va}: {have.hex(" ")}')

for name, va, dll, fn in re.findall(r'I\((\w+),\s*(0x[0-9a-fA-F]+),\s*"([^"]+)",\s*"([^"]+)"\)', addr_h):
    slot = imports.get((dll.lower(), fn))
    ok = slot == int(va, 16)
    bad += not ok
    print(('ok  ' if ok else 'FAIL') + f' iat {name} {va}: {dll}!{fn} -> {slot and hex(slot)}')

print('ИТОГ:', 'всё сходится' if not bad else f'{bad} расхождений')
sys.exit(1 if bad else 0)
