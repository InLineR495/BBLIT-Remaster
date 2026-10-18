#!/usr/bin/env python3
"""Сверка адресов кода bugs.exe в исходниках с таблицами addr.h (запускает build.sh, без exe).

Исходники пишут адреса кода раскладки A (1.6). Каждый такой адрес (0x401000..0x45c000) вне таблиц addr.h должен стоять
внутри BB_CODE(...) или BB_A(...) и быть в BB_CODES — иначе в сборках с раскладкой B (1.0) он укажет мимо.
Сайты BB_SITES и цели их call rel32 тоже должны быть в BB_CODES.
Запуск: python3 -I tools/check_addr.py"""
import pathlib
import re
import struct
import sys

CODE_LO, CODE_HI = 0x401000, 0x45c000
src = pathlib.Path(__file__).resolve().parent.parent / 'src'
addr_h = (src / 'addr.h').read_text(encoding='utf-8')
errors = []

codes = {}
for a, b in re.findall(r'C\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)', addr_h):
    a, b = int(a, 16), int(b, 16)
    if a in codes:
        errors.append(f'addr.h: {a:#x} дважды в BB_CODES')
    codes[a] = b
for name, va, hexs in re.findall(r'X\((\w+),\s*(0x[0-9a-fA-F]+),\s*"([0-9a-fA-F ]+)"\)', addr_h):
    va, raw = int(va, 16), bytes.fromhex(hexs)
    if va not in codes:
        errors.append(f'addr.h: сайт {name} {va:#x} не в BB_CODES')
    if len(raw) == 5 and raw[0] == 0xe8:
        to = va + 5 + struct.unpack('<i', raw[1:])[0]
        if to not in codes:
            errors.append(f'addr.h: цель call сайта {name} {to:#x} не в BB_CODES')


def blank(m):
    return re.sub(r'[^\n]', ' ', m.group(0))


for path in sorted(src.glob('*.[ch]')):
    text = path.read_text(encoding='utf-8')
    # комментарии и строки не код; в addr.h — ещё тела таблиц (их сверяет verify_exe.py)
    text = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', blank, text, flags=re.S)
    if path.name == 'addr.h':
        text = re.sub(r'#define\s+BB_(?:PROFILES|SITES|IATS|CODES)\(\w\)(?:[^\n]*\\\n)*[^\n]*', blank, text)
        text = re.sub(r'#define\s+BB_TEXT_VA\s+0x[0-9a-fA-F]+u?', blank, text)
    for m in re.finditer(r'0x[0-9a-fA-F]+', text):
        va = int(m.group(0), 16)
        if not CODE_LO <= va < CODE_HI:
            continue
        line = text.count('\n', 0, m.start()) + 1
        wrapped = re.search(r'\b(?:BB_CODE|BB_A)\(\s*$', text[:m.start()]) and \
            re.match(r'u?\s*\)', text[m.end():])
        if not wrapped:
            errors.append(f'{path.name}:{line}: адрес кода {m.group(0)} без BB_CODE()/BB_A()')
        elif va not in codes:
            errors.append(f'{path.name}:{line}: адрес кода {m.group(0)} не в BB_CODES (addr.h)')

for e in errors:
    print(e)
print('check_addr:', 'адреса кода сходятся с addr.h' if not errors else f'{len(errors)} ошибок')
sys.exit(1 if errors else 0)
