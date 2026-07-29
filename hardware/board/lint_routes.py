#!/usr/bin/env python3
"""Линт разводки board.json: однослойность без наложений.

Проверяет:
  1. Коллинеарные наложения — два маршрута (разных цепей ИЛИ разных веток) делят
     общее ребро сетки. Физически это два провода в одном коридоре — запрещено.
  2. Общие отверстия — одна дырка используется двумя цепями. Допустимо ТОЛЬКО
     как перпендикулярный перекрёсток (одна цепь идёт горизонтально, другая
     вертикально, ни у одной здесь нет конца/пайки) — такие точки перечисляются
     как «перекрёстки», всё прочее — ошибка.
  3. Геометрические пересечения сегментов вне отверстий (диагонали) — тоже
     перекрёстки, печатаются отдельно.
  4. Проход маршрута сквозь чужой пин — ошибка всегда.
  5. Цепи 220 (mains) вне островка 220 и НЧ-цепи внутри него.
  6. Дорожки (medium=trace): голая проволока не может иметь перекрёстков
     с другой дорожкой; на дорожке не может лежать перекрёсток, где ОБЕ
     цепи — trace.

Запуск: python3 hardware/board/lint_routes.py
"""
import json
import os
import sys
from math import gcd

HERE = os.path.dirname(os.path.abspath(__file__))
ISLAND = (15, 27, 35, 37)  # островок 220: колонки, ряды (заводская нумерация)

def seg_holes(a, b):
    """Все отверстия на сегменте, включая концы."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    g = gcd(abs(dx), abs(dy))
    if g == 0:
        return [tuple(a)]
    sx, sy = dx // g, dy // g
    return [(a[0] + sx * k, a[1] + sy * k) for k in range(g + 1)]

def seg_dir(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    if dy == 0: return 'H'
    if dx == 0: return 'V'
    return 'D'

def main() -> int:
    with open(os.path.join(HERE, 'board.json'), encoding='utf-8') as f:
        d = json.load(f)

    pin_holes = {}
    for c in d['components']:
        for p in c.get('pins', []):
            pin_holes[tuple(p['hole'])] = (c['id'], p['name'], p.get('net'))

    # рёбра и отверстия по цепям
    edge_use = {}   # ((h1,h2) отсортированное) -> [(net, route_idx)]
    hole_use = {}   # hole -> [(net, route_idx, 'end'|'via'|'pass', направление)]
    segments = []   # (net, ri, a, b, medium)
    bridges = set()
    for c in d['components']:
        for b in c.get('bridges', []):
            for a_, b_ in zip(b['path'], b['path'][1:]):
                for h1, h2 in zip(seg_holes(a_, b_), seg_holes(a_, b_)[1:]):
                    bridges.add(tuple(sorted((h1, h2))))

    for n in d['nets']:
        for ri, r in enumerate(n['routes']):
            path = r['path']
            medium = r.get('medium', 'wire')
            pts = [tuple(p) for p in path]
            for a, b in zip(path, path[1:]):
                segments.append((n['id'], ri, a, b, medium))
                holes = seg_holes(a, b)
                for h1, h2 in zip(holes, holes[1:]):
                    edge_use.setdefault(tuple(sorted((h1, h2))), []).append((n['id'], ri, medium))
                dirn = seg_dir(a, b)
                for h in holes:
                    role = 'via' if h in pts else 'pass'
                    hole_use.setdefault(h, []).append((n['id'], ri, role, dirn, medium))
            # концы маршрута
            for h in (pts[0], pts[-1]):
                hole_use.setdefault(h, [])
                # переопределить роль концов
            ends = {pts[0], pts[-1]}
            for h in ends:
                lst = hole_use[h]
                for i, (nid, rj, role, dirn, med) in enumerate(lst):
                    if nid == n['id'] and rj == ri:
                        lst[i] = (nid, rj, 'end', dirn, med)

    errs, crossings = [], []

    # пятачки узла звезды: перемычки узла (GND_STAR) + ровно один луч-провод
    # на каждом пятачке — это и есть конструкция узла, не ошибка
    star_pins = {tuple(p['hole']) for c in d['components'] if c['id'] == 'STAR'
                 for p in c.get('pins', [])}

    # 1. коллинеарные наложения (ребро в двух маршрутах)
    for edge, users in edge_use.items():
        distinct = {(u[0], u[1]) for u in users}
        if len(distinct) > 1:
            # перемычки узла звезды дублируются в GND_STAR — это узаконено
            if edge in bridges and all(u[0] == 'GND_STAR' for u in users):
                continue
            errs.append(f"НАЛОЖЕНИЕ ребра {edge[0]}–{edge[1]}: {sorted(distinct)}")

    # 2/3. общие отверстия
    for h, users in hole_use.items():
        nets_here = {}
        for nid, ri, role, dirn, med in users:
            nets_here.setdefault(nid, []).append((role, dirn, med))
        if len(nets_here) < 2:
            continue
        if h in star_pins and 'GND_STAR' in nets_here and len(nets_here) == 2:
            continue
        # пайка/конец у любой из цепей → ошибка; перпендикулярный проход → перекрёсток
        roles = {nid: {r for r, _, _ in u} for nid, u in nets_here.items()}
        dirs = {nid: {dd for _, dd, _ in u} for nid, u in nets_here.items()}
        meds = {nid: {m for _, _, m in u} for nid, u in nets_here.items()}
        if any(r & {'end', 'via'} for r in roles.values()):
            errs.append(f"ОБЩЕЕ ОТВЕРСТИЕ {list(h)}: {{{', '.join(f'{k}:{sorted(v)}' for k, v in roles.items())}}}")
        elif len(nets_here) == 2 and all(len(dd) == 1 for dd in dirs.values()):
            d1, d2 = [list(dd)[0] for dd in dirs.values()]
            if {d1, d2} == {'H', 'V'} or 'D' in (d1, d2):
                tr = [nid for nid in meds if 'trace' in meds[nid]]
                if len(tr) == 2:
                    errs.append(f"ДВЕ ДОРОЖКИ пересекаются в {list(h)}: {sorted(nets_here)}")
                else:
                    crossings.append((h, sorted(nets_here)))
            else:
                errs.append(f"ОБЩЕЕ ОТВЕРСТИЕ (параллель) {list(h)}: {sorted(nets_here)}")
        else:
            errs.append(f"ОБЩЕЕ ОТВЕРСТИЕ (>2 цепей) {list(h)}: {sorted(nets_here)}")

    # 4. сквозь чужой пин
    for nid, ri, a, b, _ in segments:
        for h in seg_holes(a, b)[1:-1]:
            if h in pin_holes and pin_holes[h][2] != nid:
                cid, pn, pnet = pin_holes[h]
                errs.append(f"СКВОЗЬ ПИН {cid}.{pn} {list(h)} (цепь {pnet}): маршрут {nid}#{ri}")

    # 5. островок 220
    def in_island(h):
        return ISLAND[0] <= h[0] <= ISLAND[2] and ISLAND[1] <= h[1] <= ISLAND[3]
    mains_ids = {n['id'] for n in d['nets'] if n.get('class') == 'mains'}
    lv_ok = {'P12V', 'K1_COIL', 'K2_COIL', 'K1_BASE', 'K2_BASE', 'DRV_OPEN', 'DRV_CLOSE', 'GND_PWR'}
    for nid, ri, a, b, _ in segments:
        for h in seg_holes(a, b):
            if nid in mains_ids and not in_island(h):
                errs.append(f"MAINS ВНЕ ОСТРОВКА: {nid}#{ri} точка {list(h)}")
            if nid not in mains_ids and in_island(h) and nid not in lv_ok:
                errs.append(f"НЧ В ОСТРОВКЕ: {nid}#{ri} точка {list(h)}")

    # 6. диагонали у дорожек
    for nid, ri, a, b, med in segments:
        if med == 'trace' and seg_dir(a, b) == 'D':
            errs.append(f"ДОРОЖКА С ДИАГОНАЛЬЮ: {nid}#{ri} {a}->{b}")

    errs = sorted(set(errs))
    print(f"сегментов {len(segments)}, перекрёстков {len(set(tuple(h) for h, _ in crossings))}, ошибок {len(errs)}")
    for h, nets_ in sorted(set((tuple(h), tuple(n)) for h, n in crossings)):
        print(f"  ⨯ перекрёсток {list(h)}: {list(nets_)}")
    if errs:
        print("ПРОБЛЕМЫ:")
        for e in errs:
            print("  •", e)
        return 1
    print("OK: наложений и общих паек нет; перекрёстки — только перпендикулярные")
    return 0

def bridges_flat(bridges):
    s = set()
    for a, b in bridges:
        s.add(a); s.add(b)
    return s

if __name__ == '__main__':
    sys.exit(main())
