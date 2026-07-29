#!/usr/bin/env python3
"""Генератор печатных листов сборки из board.json → assembly-sheets.html.

Три раздела:
  1. Нарезка — все дорожки и провода: цвет, тип, длина с запасом, откуда→куда.
  2. Чертежи — низ платы В ЗЕРКАЛЕ (как видишь перевёрнутую плату у паяльника)
     и верх (расстановка компонентов), с координатной линейкой.
  3. Прозвонка — что обязано звониться и что обязано быть в обрыве.

Запуск: python3 hardware/board/gen_sheets.py
"""
import json
import os
from math import gcd, hypot

HERE = os.path.dirname(os.path.abspath(__file__))
RAMP = {'teal': '#1D9E75', 'blue': '#2F6DB5', 'purple': '#7F77DD', 'coral': '#D9704C',
        'amber': '#BA7517', 'red': '#D4353B', 'gray': '#888780', 'green': '#639922'}
COND = {'L': '#8A5A2B', 'N': '#3D6FB4', 'L2': '#33363C', 'PE': '#86A83D'}
COND_NAME = {'L': 'коричневый', 'N': 'синий', 'L2': 'чёрный', 'PE': 'жёлто-зелёный'}

d = json.load(open(os.path.join(HERE, 'board.json'), encoding='utf-8'))
COLS, ROWS = d['board']['cols'], d['board']['rows']
PITCH = d['board']['pitch']

comp_by_id = {c['id']: c for c in d['components']}
hole_pin = {}
for c in d['components']:
    for p in c.get('pins', []):
        hole_pin[tuple(p['hole'])] = (c['id'], p['name'])

def end_name(h):
    t = hole_pin.get(tuple(h))
    return f"{t[0]}.{t[1]}" if t else f"[{h[0]},{h[1]}]"

def net_color(n):
    if n.get('class') == 'mains' and n.get('conductor') in COND:
        return COND[n['conductor']]
    return n.get('color') or RAMP.get(n.get('ramp'), '#888780')

def route_len(path, medium):
    l = sum(hypot(b[0]-a[0], b[1]-a[1]) * PITCH for a, b in zip(path, path[1:]))
    raw = l + 8 if medium == 'trace' else l * 1.15 + 32
    return int(-(-raw // 5) * 5)

def seg_holes(a, b):
    dx, dy = b[0]-a[0], b[1]-a[1]
    g = gcd(abs(dx), abs(dy))
    if g == 0: return [tuple(a)]
    return [(a[0]+dx//g*k, a[1]+dy//g*k) for k in range(g+1)]

# перекрёстки — как в lint_routes
hole_nets = {}
for n in d['nets']:
    for r in n['routes']:
        for a, b in zip(r['path'], r['path'][1:]):
            for h in seg_holes(a, b):
                hole_nets.setdefault(h, set()).add(n['id'])
crossings = sorted(h for h, s in hole_nets.items() if len(s) > 1
                   and not any(tuple(p['path'][i]) == h
                               for nn in d['nets'] for p in nn['routes']
                               for i in (0, -1)) and h not in hole_pin)

# ============ 1. Нарезка ============
rows_cut = []
for n in d['nets']:
    wire_desc = n.get('wire', '')
    for ri, r in enumerate(n['routes']):
        path = r['path']
        if len(path) < 2: continue
        med = r.get('medium', 'wire')
        rows_cut.append({
            'trace': med == 'trace', 'net': n, 'color': net_color(n),
            'len': route_len(path, med),
            'frm': end_name(path[0]), 'to': end_name(path[-1]),
            'note': r.get('note', ''), 'wire': 'лужёнка 0.5–0.8 мм' if med == 'trace' else wire_desc,
        })
def cut_table(items):
    out = ['<table><tr><th>№</th><th>Цвет</th><th>Тип</th><th>Длина</th><th>Откуда → куда</th><th>Цепь / примечание</th></tr>']
    for i, it in enumerate(items, 1):
        sw = '#cdd2da' if it['trace'] else it['color']
        out.append(f"<tr><td>{i}</td><td><i class=sw style='background:{sw}'></i></td>"
                   f"<td>{it['wire']}</td><td class=num>{it['len']} мм</td>"
                   f"<td><b>{it['frm']}</b> → <b>{it['to']}</b></td>"
                   f"<td>{it['net']['label']}{(' — ' + it['note']) if it['note'] else ''}</td></tr>")
    out.append('</table>')
    return '\n'.join(out)
traces = [x for x in rows_cut if x['trace']]
wires = [x for x in rows_cut if not x['trace']]

# ============ 2. SVG чертежи ============
S = 11          # px на отверстие
M = 34          # поле под линейку

def svg_board(mirror):
    def X(c): return M + ((COLS - c) if mirror else (c - 1)) * S
    def Y(r): return M + (r - 1) * S
    w, h = M*2 + (COLS-1)*S, M*2 + (ROWS-1)*S
    o = [f"<svg viewBox='0 0 {w} {h}' xmlns='http://www.w3.org/2000/svg' font-family='sans-serif'>"]
    o.append(f"<rect x='{M-14}' y='{M-14}' width='{(COLS-1)*S+28}' height='{(ROWS-1)*S+28}' rx='6' fill='#eaf3ec' stroke='#9db8a4'/>")
    es = d['board'].get('edgeStrips', {})
    strip_rows = set(es.get('rows', []))
    strip_cols = set(es.get('cols', []))
    # овальные столбцы коротких сторон стоят ВНЕ заводской нумерации (кол. 0 и 56)
    for c in strip_cols:
        for r in range(1, ROWS+1):
            o.append(f"<ellipse cx='{X(c)}' cy='{Y(r)}' rx='2.0' ry='1.1' fill='#b9c8bd'/>")
    for c in range(1, COLS+1):
        for r in range(1, ROWS+1):
            if r in strip_rows:
                o.append(f"<ellipse cx='{X(c)}' cy='{Y(r)}' rx='1.1' ry='2.0' fill='#b9c8bd'/>")
            else:
                o.append(f"<circle cx='{X(c)}' cy='{Y(r)}' r='1.1' fill='#b9c8bd'/>")
    # заводские буквы у буквенного (западного) края — ориентир платы
    letters = es.get('letters')
    if letters:
        pat = letters['pattern']
        for r in range(1, ROWS+1):
            o.append(f"<text x='{X(letters['col']) + (-7 if mirror else 7)}' y='{Y(r)+2.5}' font-size='6' "
                     f"text-anchor='middle' fill='#7d8a93'>{pat[(r-1) % len(pat)]}</text>")
    # крепёжные отверстия (врезаются в край сетки)
    mh = d['board'].get('mountingHoles', {})
    if isinstance(mh, dict):
        off = d['board'].get('gridOffsetMM', [0, 0])
        for mx, my in mh.get('positions', []):
            cgrid = (mx - off[0]) / PITCH + 1
            rgrid = (my - off[1]) / PITCH + 1
            o.append(f"<circle cx='{X(cgrid)}' cy='{Y(rgrid)}' r='{mh.get('diameter',3.2)/2/PITCH*S}' "
                     f"fill='#fff' stroke='#555' stroke-width='1.2'/>")
    for c in range(5, COLS+1, 5):
        o.append(f"<text x='{X(c)}' y='{M-18}' font-size='9' text-anchor='middle' fill='#555'>{c}</text>")
        o.append(f"<text x='{X(c)}' y='{M+(ROWS-1)*S+26}' font-size='9' text-anchor='middle' fill='#555'>{c}</text>")
    for r in range(5, ROWS, 5):
        o.append(f"<text x='{M-20}' y='{Y(r)+3}' font-size='9' text-anchor='end' fill='#555'>{r}</text>")
        o.append(f"<text x='{M+(COLS-1)*S+20}' y='{Y(r)+3}' font-size='9' fill='#555'>{r}</text>")
    # тела компонентов
    for c in d['components']:
        b = c.get('body')
        if not b: continue
        x1, x2 = sorted((X(b[0]), X(b[2]))); y1, y2 = Y(b[1]), Y(b[3])
        o.append(f"<rect x='{x1-4}' y='{y1-4}' width='{x2-x1+8}' height='{y2-y1+8}' rx='3' "
                 f"fill='none' stroke='#7a8a90' stroke-width='1' stroke-dasharray='3,2'/>")
        o.append(f"<text x='{(x1+x2)/2}' y='{y1-6}' font-size='8' text-anchor='middle' fill='#4a5a60'>{c['id']}</text>")
        for p in c.get('pins', []):
            hx_, hy_ = X(p['hole'][0]), Y(p['hole'][1])
            o.append(f"<circle cx='{hx_}' cy='{hy_}' r='2.4' fill='none' stroke='#333' stroke-width='1.1'/>")
    # разводка (на нижнем листе)
    if mirror:
        for n in d['nets']:
            col = net_color(n)
            for r in n['routes']:
                med = r.get('medium', 'wire')
                pts = ' '.join(f"{X(pp[0])},{Y(pp[1])}" for pp in r['path'])
                if med == 'trace':
                    o.append(f"<polyline points='{pts}' fill='none' stroke='#8d979e' stroke-width='3.4' stroke-linecap='round'/>")
                    for a, b2 in zip(r['path'], r['path'][1:]):
                        for hh in seg_holes(a, b2):
                            o.append(f"<circle cx='{X(hh[0])}' cy='{Y(hh[1])}' r='2.6' fill='#aab4bb'/>")
                else:
                    o.append(f"<polyline points='{pts}' fill='none' stroke='{col}' stroke-width='2.1' "
                             f"stroke-linecap='round' stroke-linejoin='round' opacity='0.92'/>")
        for h in crossings:
            o.append(f"<circle cx='{X(h[0])}' cy='{Y(h[1])}' r='4.2' fill='none' stroke='#e8a013' stroke-width='1.6'/>")
    o.append('</svg>')
    return '\n'.join(o)

# ============ 3. Прозвонка ============
net_points = {}
for c in d['components']:
    for p in c.get('pins', []):
        if p.get('net') and not p.get('internal'):
            net_points.setdefault(p['net'], []).append(f"{c['id']}.{p['name']} [{p['hole'][0]},{p['hole'][1]}]")
cont_rows = []
for n in d['nets']:
    pts = net_points.get(n['id'], [])
    if len(pts) >= 2:
        cont_rows.append((n, pts))
# Щупы — в ПАЙКИ на нижней стороне платы: пятачки клеммников, узел звезды
# (ряд 33, колонки 50–54), выводы понижаек и развязки.
ISOLATION = [
    ('ACIN.L', 'ACIN.N', '∞ — фаза и ноль'),
    ('ACIN.L', 'ACIN.PE', '∞'),
    ('ACIN.N', 'ACIN.PE', '∞'),
    ('FU1.OUT', 'ACOUT.OPEN', '∞ — контакт NO K1 разомкнут без питания'),
    ('FU1.OUT', 'ACOUT.CLOSE', '∞ — контакт NO K2 разомкнут'),
    ('ACOUT.OPEN', 'ACOUT.CLOSE', '∞'),
    ('ACIN.L', 'PWR.+V [48,33]', '∞ — сеть изолирована от 12 В'),
    ('ACIN.L', 'узел звезды [52,33]', '∞ — сеть изолирована от земли'),
    ('ACIN.N', 'PWR.+V [48,33]', '∞'),
    ('ACIN.N', 'узел звезды [52,33]', '∞'),
    ('PWR.+V [48,33]', 'узел звезды [52,33]', 'не КЗ: >1 кОм; короткий писк заряда конденсаторов — норма'),
    ('U5.OUT+ [21,24] (5 В)', 'узел звезды [52,33]', 'не КЗ (входы модулей)'),
    ('U4.OUT+ [3,24] (4 В)', 'узел звезды [52,33]', 'не КЗ'),
    ('C3.1 [42,6] (3.3 В)', 'узел звезды [52,33]', 'не КЗ'),
    ('PWR.+V [48,33]', 'U5.OUT+ [21,24]', '∞ / не КЗ — шины разделены понижайкой'),
    ('PWR.+V [48,33]', 'U4.OUT+ [3,24]', '∞ / не КЗ'),
    ('U5.OUT+ [21,24]', 'C3.1 [42,6]', '∞ / не КЗ'),
]

html = f"""<!doctype html><html lang=ru><meta charset=utf-8>
<meta name=viewport content="width=device-width, initial-scale=1">
<meta name=color-scheme content="light">
<title>Умные Ворота — листы сборки платы</title>
<style>
 /* печатный документ: фон всегда белый, независимо от темы браузера */
 html {{ background: #fff; }}
 body {{ font: 13px/1.5 -apple-system, 'Segoe UI', Roboto, sans-serif;
        color: #1c2126; background: #fff; margin: 24px; }}
 h1 {{ font-size: 20px; }} h2 {{ font-size: 16px; margin-top: 28px; border-bottom: 2px solid #ccc; padding-bottom: 4px; }}
 table {{ border-collapse: collapse; width: 100%; margin: 8px 0 16px; }}
 th, td {{ border: 1px solid #c9d0d6; padding: 3px 7px; text-align: left; vertical-align: top; font-size: 12px; }}
 th {{ background: #eef2f5; }} td.num {{ text-align: right; white-space: nowrap; }}
 .sw {{ display:inline-block; width: 22px; height: 11px; border-radius: 3px; border: 1px solid #0002; }}
 .page {{ page-break-after: always; }}
 svg {{ width: 100%; height: auto; border: 1px solid #dde; margin: 6px 0 14px; }}
 .note {{ background: #f6f8e8; border-left: 4px solid #b6c86a; padding: 6px 10px; margin: 8px 0; }}
 @media print {{ body {{ margin: 8mm; }} h2 {{ page-break-after: avoid; }} }}
</style>
<h1>Умные Ворота — листы сборки платы (r3)</h1>
<p>Сгенерировано из <code>board.json</code> скриптом <code>gen_sheets.py</code>. Печатать в альбомной ориентации.
Разводка однослойная: наложений и общих паек нет, пересечения — только «изолированный провод поверх» (кольца ⨯ на чертеже).</p>

<div class=page>
<h2>1. Нарезка — дорожки (голая лужёная проволока, укладывать ПЕРВЫМИ)</h2>
<div class=note>Дорожка пропаивается на <b>каждом</b> пятачке по пути. Длина указана с малым запасом.</div>
{cut_table(traces)}
<h2>1а. Нарезка — провода в изоляции</h2>
<div class=note>Длина с запасом на спуски к пятачкам, изгибы и зачистку (~15% + 32 мм). Цвет — колонка «Цвет»:
у каждой логической цепи свой; жилы 220 В: L коричневый, «закрыть» чёрный, N синий, PE жёлто-зелёный.</div>
{cut_table(wires)}
</div>

<div class=page>
<h2>2. Чертёж НИЗА платы — В ЗЕРКАЛЕ (как видишь перевёрнутую плату у паяльника)</h2>
<div class=note>Плата перевёрнута через нижний край: ряд 1 сверху, колонки идут <b>справа налево</b> —
цифры линейки совпадают с 3D-вьюером. Серые толстые линии с точками — дорожки-лужёнки;
цветные — провода; янтарные кольца — перекрёстки (провод проходит поверх).</div>
{svg_board(mirror=True)}
</div>

<div class=page>
<h2>2а. Верх платы — расстановка компонентов</h2>
{svg_board(mirror=False)}
</div>

<h2>3. Прозвонка после пайки (вилка выдернута, модули НЕ вставлены в гнёзда)</h2>
<h3>3.1 Должно звониться (0 Ом): точки одной цепи</h3>
<table><tr><th>Цепь</th><th>Точки — все звонятся между собой</th></tr>
{''.join(f"<tr><td><i class=sw style='background:{net_color(n)}'></i> {n['label']}</td><td>{' · '.join(pts)}</td></tr>" for n, pts in cont_rows)}
</table>
<h3>3.2 Обязано быть в обрыве / без КЗ</h3>
<table><tr><th>Щуп 1</th><th>Щуп 2</th><th>Ожидание</th></tr>
{''.join(f'<tr><td>{a}</td><td>{b}</td><td>{x}</td></tr>' for a, b, x in ISOLATION)}
</table>
<div class=note>Только после чистой прозвонки подавать 220 В — сначала без модулей, выставить 5.0 и 4.0 В
на выходах понижаек (щупы на пайки выходов понижаек U5.OUT+/U4.OUT+ против узла звезды), обесточить, вставить модули.</div>
</html>"""

out = os.path.join(HERE, 'assembly-sheets.html')
open(out, 'w', encoding='utf-8').write(html)
print(f"[sheets] {out}: дорожек {len(traces)}, проводов {len(wires)}, цепей в прозвонке {len(cont_rows)}, перекрёстков {len(crossings)}")
