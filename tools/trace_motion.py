# Разбор [live] trace=N (BIN\trace.bin): python3 trace_motion.py trace.bin [W H]
# 1) тайминг: кадры на пару, интервалы, «время содержимого» (тик A + α) против времени кадра;
# 2) плавность объектов: экранная позиция по формуле шейдера в каждом кадре, рывок = |Δv| / |v|;
#    худшие объекты — с режимами сглаживания по парам и движением в мире.
import struct, sys, math, collections
import numpy as np

TICK = struct.Struct('<5I4I2If2d9d3d9d3d')
OBJ = struct.Struct('<IHBB3f3f3f3f')
FRAME = struct.Struct('<2I3df')
TAGS = {1: 'block', 2: 'part', 3: 'actor', 4: 'shadow', 6: 'bb'}
MODE = [(1, 'сгл'), (2, 'плох'), (4, 'h1'), (8, 'h2'), (16, 'syn'), (32, 'скач'), (64, 'безп'), (128, 'реп')]

def mode_str(m):
    return '+'.join(n for b, n in MODE if m & b) or '-'

def dv(z):
    return z + 1.0 if z >= 0 else -z

def main():
    data = open(sys.argv[1], 'rb').read()
    W = int(sys.argv[2]) if len(sys.argv) > 2 else 1920
    H = int(sys.argv[3]) if len(sys.argv) > 3 else 1200
    ticks, frames = {}, []
    off = 0
    while off + 4 <= len(data):
        magic = struct.unpack_from('<I', data, off)[0]
        if magic == 0x4b434954:
            t = TICK.unpack_from(data, off)
            off += TICK.size
            gen, ab, bb, n = t[1], t[2], t[3], t[4]
            aonly = t[5:9]; bnew = t[9:11]; xs = t[11]; at, bt = t[12], t[13]
            cam_a = np.array(t[14:23]).reshape(3, 3); eye_a = np.array(t[23:26])
            cam_b = np.array(t[26:35]).reshape(3, 3); eye_b = np.array(t[35:38])
            objs = {}
            for k in range(n):
                o = OBJ.unpack_from(data, off)
                off += OBJ.size
                objs[(o[0], o[1])] = (o[2], o[3], o[4:7], o[7:10], o[10:13], o[13:16])
            ticks[gen] = dict(ab=ab, bb=bb, at=at, bt=bt, xs=xs, aonly=aonly, bnew=bnew, cam_a=cam_a, eye_a=eye_a,
                              cam_b=cam_b, eye_b=eye_b, objs=objs)
        elif magic == 0x4c4c5546:              # полная геометрия пары (trace_full) — здесь не нужна
            _, _, nv, _, ni, ncmd = struct.unpack_from('<6I', data, off)
            off += 24 + nv * 32 + ncmd * 16 + ni * 4
        elif magic == 0x4d415246:
            f = FRAME.unpack_from(data, off)
            off += FRAME.size
            frames.append(dict(gen=f[1], t0=f[2], ts=f[3], alpha=f[4], xs=f[5]))
        else:
            print('сбой разбора на', off)
            break
    frames = [f for f in frames if f['gen'] in ticks]
    print('пар %d, кадров %d' % (len(ticks), len(frames)))
    if not frames:
        return

    # --- тайминг ---
    per = collections.Counter(f['gen'] for f in frames)
    print('кадров на пару:', sorted(collections.Counter(per.values()).items()))
    t0 = np.array([f['t0'] for f in frames]); ts = np.array([f['ts'] for f in frames])
    d0 = np.diff(t0) * 1000; dsw = np.diff(ts) * 1000
    print('интервал начала кадра, мс: медиана %.2f, p5 %.2f, p95 %.2f, мин %.2f, макс %.2f' %
          (np.median(d0), np.percentile(d0, 5), np.percentile(d0, 95), d0.min(), d0.max()))
    print('интервал после свопа, мс:  медиана %.2f, p5 %.2f, p95 %.2f, мин %.2f, макс %.2f' %
          (np.median(dsw), np.percentile(dsw, 5), np.percentile(dsw, 95), dsw.min(), dsw.max()))
    tk = np.array([ticks[g]['bt'] - ticks[g]['at'] for g in sorted(ticks)]) * 1000
    print('шаг публикации снимков (b.t − a.t), мс: медиана %.2f, p5 %.2f, p95 %.2f, мин %.2f, макс %.2f' %
          (np.median(tk), np.percentile(tk, 5), np.percentile(tk, 95), tk.min(), tk.max()))
    # время содержимого в тиках: build A + α·(B − A); против времени после свопа (≈ показ)
    c = np.array([ticks[f['gen']]['ab'] + f['alpha'] * (ticks[f['gen']]['bb'] - ticks[f['gen']]['ab']) for f in frames])
    dc = np.diff(c)
    print('шаг содержимого за кадр, тиков: медиана %.3f, p5 %.3f, p95 %.3f, мин %.3f, макс %.3f (ровно 0,25 при 120 Гц)' %
          (np.median(dc), np.percentile(dc, 5), np.percentile(dc, 95), dc.min(), dc.max()))
    A = np.vstack([ts - ts[0], np.ones_like(ts)]).T
    k, b0 = np.linalg.lstsq(A, c, rcond=None)[0]
    res = (c - (k * (ts - ts[0]) + b0)) / k * 1000
    print('содержимое против показа: %.2f тик/с, отклонение мс: σ %.2f, p95 |%.2f|, макс |%.2f|' %
          (k, res.std(), np.percentile(np.abs(res), 95), np.abs(res).max()))
    # кадры с нерегулярным шагом содержимого
    bad_dc = np.where(np.abs(dc - np.median(dc)) > 0.08)[0]
    print('кадров с шагом содержимого ±0,08 тика от медианы: %d из %d' % (len(bad_dc), len(dc)))

    # --- P23 по парам ---
    ao = np.array([ticks[g]['aonly'] for g in sorted(ticks)]); bn = np.array([ticks[g]['bnew'] for g in sorted(ticks)])
    print('P23 на пару (среднее/макс): граней A без B блоки %.1f+%.1f/%d, акторы %.1f+%.1f/%d; граней B без A блоки %.1f/%d, акторы %.1f/%d' %
          (ao[:, 0].mean(), ao[:, 1].mean(), (ao[:, 0] + ao[:, 1]).max(), ao[:, 2].mean(), ao[:, 3].mean(),
           (ao[:, 2] + ao[:, 3]).max(), bn[:, 0].mean(), bn[:, 0].max(), bn[:, 1].mean(), bn[:, 1].max()))

    # --- поворот камеры по парам ---
    rot = []
    for g in sorted(ticks):
        T = ticks[g]
        try:
            m = T['cam_b'] @ np.linalg.inv(T['cam_a'])
            cs = max(-1, min(1, (np.trace(m) - 1) / 2))
            rot.append(math.degrees(math.acos(cs)))
        except np.linalg.LinAlgError:
            rot.append(0)
    rot = np.array(rot)
    print('поворот камеры за тик, °: медиана %.2f, p95 %.2f, макс %.2f; пар с поворотом > 1°: %d' %
          (np.median(rot), np.percentile(rot, 95), rot.max(), (rot > 1).sum()))

    # --- траектории ---
    tracks = collections.defaultdict(list)    # ключ → [(кадр, x, y, режим, тег, gen)]
    for fi, f in enumerate(frames):
        T = ticks[f['gen']]; t = f['alpha']; xs = f['xs']
        for key, (tag, mode, pa, pb, ra, rb) in T['objs'].items():
            if pa == pb:
                x, y = pb[0], pb[1]
            else:
                z = pa[2] + (pb[2] - pa[2]) * t
                da, db, d = dv(pa[2]), dv(pb[2]), dv(z)
                x = (pa[0] * da * (1 - t) + pb[0] * db * t) / d
                y = (pa[1] * da * (1 - t) + pb[1] * db * t) / d
            x *= xs
            if abs(x) > 1 or abs(y) > 1:
                continue
            tracks[key].append((fi, x * W / 2, y * H / 2, mode, tag, f['gen']))
    rows = []
    for key, tr in tracks.items():
        if len(tr) < 16:
            continue
        fi = np.array([r[0] for r in tr]); p = np.array([[r[1], r[2]] for r in tr])
        ok = np.diff(fi) == 1
        v = np.diff(p, axis=0)[ok]
        if len(v) < 12:
            continue
        sp = np.linalg.norm(v, axis=1)
        jk = np.linalg.norm(np.diff(v, axis=0), axis=1)
        if sp.mean() < 0.5:
            continue
        modes = collections.Counter(r[3] for r in tr)
        rows.append((jk.mean() / sp.mean(), sp.mean(), jk.max(), key, TAGS.get(tr[0][4], str(tr[0][4])), modes, len(tr)))
    if not rows:
        print('движущихся траекторий нет')
        return
    rows.sort(key=lambda r: -r[0])
    ratios = np.array([r[0] for r in rows])
    blocks = np.array([r[0] for r in rows if r[4] == 'block'])
    print('траекторий в движении: %d; рывок |Δv|/|v|: медиана %.3f, у блоков %.3f, p90 %.3f' %
          (len(rows), np.median(ratios), np.median(blocks) if len(blocks) else -1, np.percentile(ratios, 90)))
    by_tag = collections.defaultdict(list)
    for r in rows:
        by_tag[r[4]].append(r[0])
    for tg, v in sorted(by_tag.items()):
        print('  %-6s n=%4d медиана %.3f p90 %.3f' % (tg, len(v), np.median(v), np.percentile(v, 90)))
    print('худшие:')
    for r in rows[:25]:
        print('  %.3f скорость %5.2f px/кадр макс|Δv| %6.2f  %08x/%-4d %-6s кадров %4d  режимы %s' %
              (r[0], r[1], r[2], r[3][0], r[3][1], r[4], r[6],
               ' '.join('%s:%d' % (mode_str(m), n) for m, n in r[5].most_common(4))))
    # подробно: худшие 6 — скорость по кадрам с режимом
    for r in rows[:6]:
        tr = tracks[r[3]]
        print('--- %08x/%d %s' % (r[3][0], r[3][1], r[4]))
        line = []
        for a, b in zip(tr, tr[1:]):
            if b[0] != a[0] + 1:
                continue
            vx, vy = b[1] - a[1], b[2] - a[2]
            mark = '|' if b[5] != a[5] else ' '
            line.append('%s%+6.1f,%+5.1f %s' % (mark, vx, vy, mode_str(b[3])))
        for i in range(0, min(len(line), 48), 8):
            print('   ', '  '.join(line[i:i + 8]))

main()
