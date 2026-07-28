#!/usr/bin/env python3
"""Проверка board.json: геометрия, пины, цепи, шаги сборки.

Запуск: python3 hardware/board/validate.py
Гоняем после каждой правки раскладки — дешевле, чем найти наложение с паяльником в руках.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    with open(os.path.join(HERE, "board.json"), encoding="utf-8") as f:
        d = json.load(f)

    cols, rows = d["board"]["cols"], d["board"]["rows"]
    errs = []

    def inside(col, row):
        return 1 <= col <= cols and 1 <= row <= rows

    # --- геометрия: тела компонентов в пределах платы и не налезают друг на друга ---
    occupied = {}
    for c in d["components"]:
        x1, y1, x2, y2 = c["body"]
        if not (inside(x1, y1) and inside(x2, y2)):
            errs.append(f"{c['id']}: тело за краем платы {c['body']}")
        for col in range(x1, x2 + 1):
            for row in range(y1, y2 + 1):
                occupied.setdefault((col, row), []).append(c["id"])

    overlaps = {}
    for ids in occupied.values():
        if len(ids) > 1:
            key = tuple(sorted(set(ids)))
            overlaps[key] = overlaps.get(key, 0) + 1
    for pair, n in overlaps.items():
        errs.append(f"пересечение тел {' и '.join(pair)}: {n} отверстий")

    # --- пины: в пределах платы, по одному на отверстие ---
    holes = {}
    for c in d["components"]:
        for p in c.get("pins", []):
            col, row = p["hole"]
            if not inside(col, row):
                errs.append(f"{c['id']}.{p['name']}: пин за краем платы {p['hole']}")
            holes.setdefault((col, row), []).append(f"{c['id']}.{p['name']}")
    for hole, names in holes.items():
        if len(names) > 1:
            errs.append(f"два пина в одном отверстии {list(hole)}: {', '.join(names)}")

    # --- цепи: объявлены ровно те, что стоят на пинах ---
    declared = {n["id"] for n in d["nets"]}
    used = {p["net"] for c in d["components"] for p in c.get("pins", []) if "net" in p}
    if used - declared:
        errs.append(f"цепи есть на пинах, но не объявлены: {sorted(used - declared)}")
    if declared - used:
        errs.append(f"цепи объявлены, но ни на одном пине: {sorted(declared - used)}")

    # --- маршруты: в пределах платы, и каждый пин цепи ими задет ---
    # Пин может стоять в середине маршрута — это «ромашка» на одном пятачке, так и паяют.
    pins_of_net = {}
    for c in d["components"]:
        for p in c.get("pins", []):
            if "net" in p and not p.get("internal"):
                pins_of_net.setdefault(p["net"], set()).add(tuple(p["hole"]))

    for net in d["nets"]:
        touched = set()
        for route in net["routes"]:
            for col, row in route["path"]:
                if not (inside(col, row) or net.get("offBoard")):
                    errs.append(f"{net['id']}: точка маршрута за краем платы {[col, row]}")
                touched.add((col, row))
        missed = pins_of_net.get(net["id"], set()) - touched
        if missed:
            errs.append(
                f"{net['id']}: пины не подключены маршрутом: "
                + ", ".join(str(list(m)) for m in sorted(missed))
            )

    # --- нижний ярус (powerBoards): геометрия каждой платы и уникальность id ---
    comp_ids = {c["id"] for c in d["components"]}
    pb_ids = set()
    for pboard in d.get("powerBoards", {}).get("boards", []):
        pcols, prows = pboard["board"]["cols"], pboard["board"]["rows"]
        for c in pboard["components"]:
            pb_ids.add(c["id"])
            x1, y1, x2, y2 = c["body"]
            if not (1 <= x1 <= pcols and 1 <= x2 <= pcols and 1 <= y1 <= prows and 1 <= y2 <= prows):
                errs.append(f"{pboard['id']} {c['id']}: тело за краем платы {c['body']}")
            for p in c.get("pins", []):
                col, row = p["hole"]
                if not (1 <= col <= pcols and 1 <= row <= prows):
                    errs.append(f"{pboard['id']} {c['id']}.{p['name']}: пин за краем {p['hole']}")
    if pb_ids & comp_ids:
        errs.append(f"id повторяются между ярусами: {pb_ids & comp_ids}")

    # --- узлы в корпусе: связи ведут к существующим компонентам или узлам ---
    enc = d.get("enclosure", {})
    enc_ids = {n["id"] for n in enc.get("nodes", [])}
    for link in enc.get("links", []):
        for end in ("from", "to"):
            if link[end] not in comp_ids | enc_ids | pb_ids:
                errs.append(f"связь в корпусе ведёт в никуда: {link[end]}")

    # --- шаги сборки ссылаются на существующие объекты ---
    zone_ids = {z["id"] for z in d["zones"]}
    for s in d["steps"]:
        hl = s.get("highlight", {})
        for x in hl.get("components", []):
            if x not in comp_ids:
                errs.append(f"шаг {s['n']}: нет компонента {x}")
        for x in hl.get("nets", []):
            if x not in declared:
                errs.append(f"шаг {s['n']}: нет цепи {x}")
        for x in hl.get("zones", []):
            if x not in zone_ids:
                errs.append(f"шаг {s['n']}: нет зоны {x}")

    pitch = d["board"]["pitch"]
    print(
        f"плата {cols}x{rows} отверстий = {(cols - 1) * pitch:.0f}x{(rows - 1) * pitch:.0f} мм | "
        f"компонентов {len(d['components'])}, цепей {len(d['nets'])}, шагов {len(d['steps'])}"
    )

    def center(cid):
        x1, y1, x2, y2 = next(c for c in d["components"] if c["id"] == cid)["body"]
        return ((x1 + x2) / 2, (y1 + y2) / 2)

    def dist_mm(a, b):
        return (((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5) * pitch

    print("разнос узлов:")
    for label, a, b in [
        ("CC1101 ↔ SIM800L", "U2", "U3"),
        ("CC1101 ↔ понижайка 5 В", "U2", "U5"),
        ("CC1101 ↔ реле K1", "U2", "K1"),
        ("понижайка 4 В ↔ SIM800L", "U4", "U3"),
    ]:
        print(f"  {label}: {dist_mm(center(a), center(b)):.0f} мм")

    if errs:
        print("\nПРОБЛЕМЫ:")
        for e in errs:
            print("  •", e)
        return 1

    print("\nOK: наложений нет, цепи сходятся, шаги ссылаются на существующее")
    return 0


if __name__ == "__main__":
    sys.exit(main())
