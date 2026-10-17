#!/usr/bin/env python3
"""Сводка логов обхода уровней (sweep_levels.sh): пики по строкам P1 для каждого /bNN.
Запуск: python3 -I tools/sweep_report.py <каталог с NN.log>"""
import pathlib
import re
import sys

NAMES = ('TITLE LOADING LANGUAGE LS01 L01A L01B L01C L01D1 L01D2 L02A1 L02A2 L02A3 L02A4 L02A5 L02A6 L02B1 L02B2 '
         'L02C1 L02C2 L02C3 L02C4 L03A L03ACOM L03A2 L03B L03C L03C1 L03C2 L03D1 L04A1 L04A2 L04A3 L04B1 L04B2 '
         'L04B3 L04C1 L04C2 L04C3 L04D1 L04D2 L04E L04E2 L05A1 L05A2 L05A3A L05A3B L05A3C L05A4 L05A5 L05B1 L05C '
         'MERLIN LB01 LB02 LB04 LB05 CCINTRO CC1A CC2A CC2B CC2C CC3A CC3B CC3C CC3D CC4A CC4B CC4C CC4D CC5A').split()
FIELDS = {'N_pre': r'N_pre=(\d+)', 'post': r'post=(\d+)', 'pool': r'pool=(\d+)', 'prims': r'prims=(\d+)',
          'emitA': r'emit=(\d+)/', 'emitB': r'emit=\d+/(\d+)/', 'drawmax': r'draw avg=[\d.]+ max=([\d.]+)',
          'skips': r'skips=\+(\d+)', 'mism': r' mism=(\d+) \|'}
d = pathlib.Path(sys.argv[1])
print(f"{'lvl':>4} {'name':8} {'s':>3} {'N_pre':>6} {'%2000':>6} {'pool':>6} {'%35k':>5} {'prims':>6} "
      f"{'arenaA':>7} {'%1M':>5} {'bzeB':>7} {'draw':>6} {'skip/s':>6} {'mism':>4} veh")
rows = {}
for f in sorted(d.glob('*.log')):
    text = f.read_text(encoding='utf-8', errors='replace')
    veh = text.count('VEH #')
    for l in text.splitlines():
        m = re.search(r' P1 .*?lvl=([0-9a-f]{8})', l)
        if not m or 'draws=0 ' in l:
            continue
        lvl = int(m.group(1), 16)
        r = rows.setdefault(lvl, {'s': 0, 'skips': 0, 'mism': 0, 'veh': 0, 'src': set(), **{k: 0.0 for k in FIELDS}})
        r['s'] += 1
        r['src'].add(f.stem)
        r['veh'] = max(r['veh'], veh)
        for k, rx in FIELDS.items():
            mm = re.search(rx, l)
            if not mm:
                continue
            v = float(mm.group(1))
            if k == 'skips':
                r['skips'] += v if v < 1000 else 0   # сброс счётчика на смене уровня
            elif k == 'mism':
                r['mism'] += v
            else:
                r[k] = max(r[k], v)
for n, r in sorted(rows.items()):
    name = NAMES[n] if n < len(NAMES) else '?'
    print(f"{n:>4} {name:8} {r['s']:>3} {r['N_pre']:>6.0f} {r['N_pre'] / 20:>5.1f}% "
          f"{r['pool']:>6.0f} {r['pool'] / 350:>4.1f}% {r['prims']:>6.0f} {r['emitA']:>7.0f} {r['emitA'] / 1e4:>4.1f}% "
          f"{r['emitB']:>7.0f} {r['drawmax']:>6.2f} {r['skips'] / r['s']:>6.2f} {r['mism']:>4.0f} {r['veh']}  (/b{','.join(sorted(r['src']))})")
