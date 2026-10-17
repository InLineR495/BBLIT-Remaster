# Растеризация кадра из [live] trace=N trace_full=1 (BIN\trace.bin) — кто виден в области экрана:
#   python3 trace_raster.py trace.bin кадр x0 y0 x1 y1 [W H] [out.png]
# Кадр — номер в trace (с 0); координаты — пиксели W×H (по умолчанию 640×400, как cap_div=3), y сверху.
# Повторяет шейдер: позиция — линейно в виде (pa·dv, pb·dv), x мира ×xs кадра; порядок списков 3, 5, 4, 1
# (2 — задник, 0 — экран не рисуются), глубина по w = −z, грани A (список 5) чуть дальше.
import struct, sys, collections
import numpy as np

TICK = struct.Struct('<5I4I2If2d9d3d9d3d'); OBJ = 56; FRAME = struct.Struct('<2I3df')
TV = np.dtype([('pa', '<f4', 3), ('pb', '<f4', 3), ('obj', '<u4'), ('local', '<u2'), ('tag', 'u1'), ('mode', 'u1')])
TC = np.dtype([('list', 'u1'), ('trans', 'u1'), ('abr', 'i1'), ('pad', 'u1'), ('first', '<u4'), ('count', '<u4'), ('tex', '<u4')])
MODE = [(1, 'сгл'), (2, 'плох'), (4, 'h1'), (8, 'h2'), (16, 'syn'), (32, 'скач'), (64, 'безп'), (128, 'реп')]
TAGS = {1: 'block', 2: 'part', 3: 'actor', 4: 'shadow', 6: 'bb'}

def mstr(m):
    return 'A-грань' if m == 255 else ('+'.join(n for b, n in MODE if m & b) or '-')

def load(path):
    data = open(path, 'rb').read()
    off, full, frames, ticks = 0, {}, [], {}
    while off < len(data):
        magic = struct.unpack_from('<I', data, off)[0]
        if magic == 0x4b434954:
            t = TICK.unpack_from(data, off)
            ticks[t[1]] = t
            off += TICK.size + t[4] * OBJ
        elif magic == 0x4c4c5546:
            _, gen, nv, nvb, ni, ncmd = struct.unpack_from('<6I', data, off)
            off += 24
            v = np.frombuffer(data, TV, nv, off); off += nv * 32
            c = np.frombuffer(data, TC, ncmd, off); off += ncmd * 16
            ix = np.frombuffer(data, '<u4', ni, off); off += ni * 4
            full[gen] = (nvb, v, c, ix)
        elif magic == 0x4d415246:
            frames.append(FRAME.unpack_from(data, off)); off += FRAME.size
        else:
            raise SystemExit('сбой разбора на %d' % off)
    return ticks, full, frames

def dv(z):
    return np.where(z >= 0, z + 1.0, np.maximum(-z, 1e-4))

def main():
    ticks, full, frames = load(sys.argv[1])
    fi = int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7])
    W = int(sys.argv[7]) if len(sys.argv) > 7 else 640
    H = int(sys.argv[8]) if len(sys.argv) > 8 else 400
    out = sys.argv[9] if len(sys.argv) > 9 else None
    _, gen, _, _, alpha, xs = frames[fi]
    if gen not in full:
        raise SystemExit('нет полной геометрии для пары %d' % gen)
    nvb, v, cmds, ix = full[gen]
    t = ticks[gen]
    print('кадр %d: пара %d (тик %d→%d), α %.3f, xs %.3f, вершин %d (+%d граней A), команд %d' %
          (fi, gen, t[2], t[3], alpha, xs, nvb, len(v) - nvb, len(cmds)))
    pa, pb = v['pa'].astype(np.float64), v['pb'].astype(np.float64)
    same = np.all(pa == pb, axis=1)
    z = np.where(same, pb[:, 2], pa[:, 2] + (pb[:, 2] - pa[:, 2]) * alpha)
    na = pa[:, :2] * dv(pa[:, 2])[:, None]; nb = pb[:, :2] * dv(pb[:, 2])[:, None]
    hx = np.where(same[:, None], nb, na + (nb - na) * alpha)      # = n·d — однородные x, y (вид × H)
    w = -z
    sxk = np.where(v['tag'] == 6, 0.8333, xs)
    X = (hx[:, 0] * sxk / np.maximum(w, 1e-6) + 1) * 0.5 * W
    Y = (1 - hx[:, 1] / np.maximum(w, 1e-6)) * 0.5 * H
    depth = np.full((H, W), np.inf); idb = np.full((H, W), -1, np.int64)
    tri_info = []
    order = {3: 0, 5: 1, 4: 2, 1: 3}
    for c in sorted(cmds, key=lambda c: order.get(int(c['list']), 9)):
        L = int(c['list'])
        if L not in order:
            continue
        tri = ix[c['first']:c['first'] + c['count']].reshape(-1, 3)
        for k in tri:
            if np.any(w[k] < 8.0):
                continue
            px, py, iw = X[k], Y[k], 1.0 / w[k]
            bx0, bx1 = int(max(np.floor(px.min()), 0)), int(min(np.ceil(px.max()), W - 1))
            by0, by1 = int(max(np.floor(py.min()), 0)), int(min(np.ceil(py.max()), H - 1))
            if bx0 > bx1 or by0 > by1:
                continue
            gx, gy = np.meshgrid(np.arange(bx0, bx1 + 1) + 0.5, np.arange(by0, by1 + 1) + 0.5)
            d = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
            if abs(d) < 1e-9:
                continue
            l0 = ((py[1] - py[2]) * (gx - px[2]) + (px[2] - px[1]) * (gy - py[2])) / d
            l1 = ((py[2] - py[0]) * (gx - px[2]) + (px[0] - px[2]) * (gy - py[2])) / d
            l2 = 1 - l0 - l1
            ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ins.any():
                continue
            dep = 1.0 / (l0 * iw[0] + l1 * iw[1] + l2 * iw[2])
            if L == 5:
                dep = dep * 1.002                       # сдвиг глубины назад
            tid = len(tri_info)
            tri_info.append((L, int(c['tex']), int(c['trans']), k.copy()))
            sub = depth[by0:by1 + 1, bx0:bx1 + 1]; sid = idb[by0:by1 + 1, bx0:bx1 + 1]
            win = ins & (dep < sub)
            if c['trans']:
                continue                                # полупрозрачное глубину не пишет — для ID не берём
            sub[win] = dep[win]; sid[win] = tid
    reg = idb[y0:y1, x0:x1]
    cnt = collections.Counter(reg.ravel().tolist())
    print('область (%d,%d)-(%d,%d): пикселей %d, без геометрии (небо/задник) %d' % (x0, y0, x1, y1, reg.size, cnt.get(-1, 0)))
    for tid, n in cnt.most_common(25):
        if tid < 0:
            continue
        L, tex, tr, k = tri_info[tid]
        vv = v[k]
        objs = sorted(set('%08x' % o for o in vv['obj']))
        print('  треуг %5d список %d tex %4d пикс %5d  объект %s  тег %s  режимы %s' %
              (tid, L, tex, n, ','.join(objs), TAGS.get(int(vv['tag'][0]), vv['tag'][0]),
               ' '.join('%d:%s' % (int(l), mstr(int(m))) for l, m in zip(vv['local'], vv['mode']))))
        for j in k:
            print('        v%-6d local %4d pa (%.3f %.3f %.0f) pb (%.3f %.3f %.0f) экран (%.1f, %.1f) w %.0f' %
                  (j, v['local'][j], *v['pa'][j], *v['pb'][j], X[j], Y[j], w[j]))
    if out:
        import cv2
        img = np.zeros((H, W, 3), np.uint8)
        ids = idb.copy(); m = ids >= 0
        rng = np.random.default_rng(1)
        pal = rng.integers(40, 255, (len(tri_info) + 1, 3)).astype(np.uint8)
        img[m] = pal[ids[m]]
        cv2.rectangle(img, (x0, y0), (x1, y1), (255, 255, 255), 1)
        cv2.imwrite(out, img[:, :, ::-1])

main()
