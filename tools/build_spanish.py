#!/usr/bin/env python3
"""Build an experimental profile for one verified Spanish-installation EXE.

No game executable is modified or copied into the build output. The source
profile retains the CRC and hook-byte checks. Requires i686 MinGW-w64.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SHA256 = '3eae4e369d79a02d0b3cafeb7c5f80f6bbba7cf4ffc02a56876aaa885d5bc050'

# Explicit mappings, not a global relocation delta. Sites were matched against
# the code; writers against Reinit's dispatch assignments; return addresses
# against call sites; video apply against actionTable[0x76].
CODE = {
    0x401000: 0x401000, 0x403010: 0x403010, 0x405ea9: 0x405ea9,
    0x4065e0: 0x406600, 0x40660b: 0x40662b,
    0x406851: 0x406871, 0x4069c0: 0x4069e0,
    0x406be1: 0x406c01, 0x406d4c: 0x406d6c, 0x406ee0: 0x406f00,
    0x409a10: 0x409a40,
    0x40a6c0: 0x40a720, 0x40b130: 0x40b190,
    0x40b980: 0x40b9e0, 0x40be10: 0x40be70,
    0x40c5e0: 0x40c640, 0x40c850: 0x40c8b0,
    0x40cc80: 0x40cce0, 0x40ceb0: 0x40cf10,
    0x40e323: 0x40e37f, 0x40e790: 0x40e820,
    0x40fa90: 0x40fb20, 0x40fdc0: 0x40fe50,
    0x410070: 0x410100, 0x4100ba: 0x41014a,
    0x410b62: 0x410bf2, 0x410bbe: 0x410c4e, 0x410c15: 0x410ca5,
    0x410f6f: 0x410fff, 0x410f74: 0x411004,
    0x410f90: 0x411020, 0x410fb4: 0x411044, 0x410fc0: 0x411050,
    0x411350: 0x4113e0, 0x411c7d: 0x411d0d,
    0x4124b0: 0x412540, 0x414590: 0x414620, 0x414b40: 0x414bd0,
    0x414c0a: 0x414c9a,
    0x42e050: 0x42e0f0, 0x42e7a0: 0x42e910,
    0x4323ce: 0x43253e, 0x432436: 0x4325a6,
    0x447a50: 0x447bc0, 0x447f3a: 0x4480aa,
    0x448074: 0x4481e4, 0x4481c7: 0x448337, 0x4481d1: 0x448341,
    0x4484b7: 0x448627, 0x4484bd: 0x44862d,
    0x448d40: 0x448eb0, 0x4491c0: 0x449330, 0x449223: 0x449393,
    0x44b456: 0x44b5c6, 0x44b482: 0x44b5f2,
    0x44be06: 0x44bf76, 0x44c981: 0x44caf1, 0x44ca31: 0x44cba1,
    0x451eb0: 0x452050, 0x451eb5: 0x452055, 0x45b400: 0x45b400,
}
IAT = {
    0x9cc28c: 0x9cc290, 0x9cc290: 0x9cc294,
    0x9cc2c8: 0x9cc2cc, 0x9cc2d8: 0x9cc2dc, 0x9cc2dc: 0x9cc2e0,
    0x9cc41c: 0x9cc428, 0x9cc3fc: 0x9cc3f8,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--cc', default='i686-w64-mingw32-gcc')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        parser.error('This profile requires the exact executable documented in docs/spanish-executable.md')
    dest = ROOT / 'build' / 'spanish'
    (dest / 'src').mkdir(parents=True, exist_ok=True)
    (dest / 'tools').mkdir(exist_ok=True)
    mapping = CODE | IAT
    for path in (ROOT / 'src').iterdir():
        if path.suffix not in ('.c', '.h', '.def'):
            continue
        source = path.read_text(encoding='utf8')
        # Fail if upstream introduces another literal code address that has
        # not been audited for this profile. Ignore comments and strings here.
        code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', source, flags=re.S)
        for token in re.findall(r'0x[0-9a-fA-F]+', code):
            va = int(token, 16)
            if 0x401000 <= va < 0x45c000 and va not in CODE:
                raise ValueError(f'Unaudited code address in {path.name}: {token}')
        source = re.sub(r'0x[0-9a-fA-F]+',
                        lambda m: f'0x{mapping[int(m[0],16)]:x}' if int(m[0],16) in mapping else m[0], source)
        if path.name == 'addr.h':
            source = source.replace('"bugs.exe 1.6"', '"Spanish-installation experimental profile"')
            source = source.replace('0xecffb7c9u', '0x4df536d5u')
            source = source.replace('0x3fe5555555555555ull', '0x3fe2d2d2d2d2d2d3ull')
            # Relative call operands change independently of their sites.
            for name, site, target in [('N1_PADUPD', 0x40662b, 0x449330),
                                       ('N2_MAPPAD', 0x449393, 0x403010)]:
                expect = (b'\xe8' + struct.pack('<i', target-site-5)).hex(' ')
                source, count = re.subn(r'(X\('+name+r',\s*0x[0-9a-f]+,\s*")[^"]+',
                                        lambda m: m[1]+expect, source)
                assert count == 1
        if path.name == 'remaster.h':
            source = source.replace('#define BB_VERSION "1.0-beta"',
                                    '#define BB_VERSION "1.0-beta-es-experimental"')
        (dest / 'src' / path.name).write_text(source, encoding='utf8')
    shutil.copy2(ROOT / 'tools' / 'verify_exe.py', dest / 'tools' / 'verify_exe.py')
    subprocess.run([sys.executable, '-I', str(dest/'tools'/'verify_exe.py'), str(args.exe.resolve())], check=True)
    if not args.prepare_only:
        subprocess.run([args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter',
                        '-shared', '-static-libgcc', '-Wl,--enable-stdcall-fixup',
                        '-o', str(dest/'ddraw.dll'),
                        *map(str, sorted((dest/'src').glob('*.c'))),
                        str(dest/'src'/'ddraw.def'), '-lgdi32'], check=True)
        print(f'Built {dest / "ddraw.dll"}')


if __name__ == '__main__':
    main()
