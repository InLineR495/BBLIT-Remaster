#!/usr/bin/env python3
"""Check a bugs.exe against src/addr.h: which known edition it is (BB_PROFILES: .text CRC32 and marker), then every
hook site (BB_SITES, bytes at the edition's address) and import slot (BB_IATS). Read-only, no extra packages.
Usage: python3 -I tools/verify_exe.py [path to bugs.exe] (default ../orig/bugs.exe)"""
import hashlib
import pathlib
import re
import struct
import sys
import zlib

here = pathlib.Path(__file__).resolve().parent.parent
exe = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent / 'orig' / 'bugs.exe'
addr_h = (here / 'src' / 'addr.h').read_text(encoding='utf-8')
d = exe.read_bytes()
print(f'File: {exe}')
print(f'Size: {len(d)} bytes')
print(f'SHA-256: {hashlib.sha256(d).hexdigest()}')

pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
entry = base + struct.unpack_from('<I', d, pe + 24 + 16)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + optsz + 40 * i + 8) for i in range(nsec)]
print(f'Image base: {base:#010x}; entry point: {entry:#010x}')


def off(va):
    for vs, rva, rs, ra in secs:
        if base + rva <= va < base + rva + max(vs, rs):
            return ra + va - base - rva
    raise ValueError(hex(va))


def const(name):
    return int(re.search(r'#define\s+' + name + r'\s+(0x[0-9a-fA-F]+)', addr_h).group(1), 16)


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

profiles = [(pid, name, int(crc, 16), int(marker, 16), int(layout)) for pid, name, crc, marker, layout in re.findall(
    r'P\((\w+),\s*"([^"]+)",\s*(0x[0-9a-fA-F]+)u,\s*(0x[0-9a-fA-F]+)ull,\s*(\d)\)', addr_h)]
codes = {int(a, 16): int(b, 16) for a, b in re.findall(r'C\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)', addr_h)}
sites = [(n, int(va, 16), bytes.fromhex(h)) for n, va, h in re.findall(
    r'X\((\w+),\s*(0x[0-9a-fA-F]+),\s*"([0-9a-fA-F ]+)"\)', addr_h)]
iats = [(n, int(a, 16), int(b, 16), dll, fn) for n, a, b, dll, fn in re.findall(
    r'I\((\w+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*"([^"]+)",\s*"([^"]+)"\)', addr_h)]

t0 = off(const('BB_TEXT_VA'))
crc = zlib.crc32(d[t0:t0 + const('BB_TEXT_SIZE')])
marker = struct.unpack_from('<Q', d, off(const('BB_MARKER_ADDR')))[0]
print(f'.text CRC32: {crc:#010x}; marker @{const("BB_MARKER_ADDR"):#x}: {marker:#018x}')


def code(va, layout):
    return va if layout == 0 else codes.get(va)


def check(layout, verbose):
    bad = 0
    for name, va_a, want in sites:
        va = code(va_a, layout)
        if va is not None and len(want) == 5 and want[0] == 0xe8:     # call rel32: the target moves too
            to = code(va_a + 5 + struct.unpack('<i', want[1:])[0], layout)
            want = b'\xe8' + struct.pack('<i', to - (va + 5)) if to is not None else None
        have = d[off(va):off(va) + len(want)] if va is not None and want else b''
        ok = va is not None and have == want
        bad += not ok
        if verbose:
            print(('ok  ' if ok else 'FAIL') + f' site {name} {va or 0:#x}: {have.hex(" ")}')
    for name, va_a, va_b, dll, fn in iats:
        slot, want = imports.get((dll.lower(), fn)), (va_a, va_b)[layout]
        ok = slot == want
        bad += not ok
        if verbose:
            print(('ok  ' if ok else 'FAIL') + f' iat {name} {want:#x}: {dll}!{fn} -> {slot and hex(slot)}')
    return bad


if base != const('BB_IMAGE_BASE'):
    print(f'RESULT: image base {base:#x} is not {const("BB_IMAGE_BASE"):#x}: unsupported executable')
    sys.exit(1)
match = next((p for p in profiles if p[2] == crc and p[3] == marker), None)
def cd_check(layout):
    va = code(int(re.search(r'#define\s+BB_CD_CHECK_VA\s+BB_A\((0x[0-9a-fA-F]+)', addr_h).group(1), 16), layout)
    have = d[off(va):off(va) + 8]
    for key, state in (('BB_CD_CHECK_BYTES', 'present (the mod removes it in memory)'),
                       ('BB_CD_CHECK_REMOVED', 'already removed in this exe')):
        want = bytes.fromhex(re.search(r'#define\s+' + key + r'\s+"([0-9a-fA-F ]+)"', addr_h).group(1))
        if have.startswith(want):
            print(f'ok   startup CD check {va:#x}: {state}')
            return 0
    print(f'FAIL startup CD check {va:#x}: unexpected bytes {have.hex(" ")}')
    return 1


if match:
    print(f'Edition: {match[1]} (profile {match[0]}, code layout {"AB"[match[4]]})')
    bad = check(match[4], True) + cd_check(match[4])
    print('RESULT:', f'supported: {match[1]}' if not bad else f'{bad} mismatches: do not use this build with this exe')
    sys.exit(1 if bad else 0)
print('Edition: unknown (no profile in src/addr.h has this .text CRC32 and marker)')
for layout in (0, 1):
    n = len(sites) + len(iats)
    print(f'  code layout {"AB"[layout]}: {n - check(layout, False)} of {n} sites and import slots match')
print('RESULT: unsupported executable; the mod stays off with it. See docs/porting-editions.md')
sys.exit(1)
