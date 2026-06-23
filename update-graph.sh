#!/bin/bash
# ============================================================================
# update-graph.sh — чистая пересборка графа знаний (graphify) для этого проекта.
#
# Отличия от встроенного `graphify hook`:
#   - исключает graphify-out/ (само-скан), .claude/ (воркстри), data/static/ и
#     минифицированный main.*.js — иначе граф засоряется
#   - НЕ генерирует graph.html (не нужен)
#   - стабильные имена сообществ (labels ниже)
#
# Ограничение: семантическую экстракцию (docs) делает LLM-субагент — скрипт её
# НЕ запускает, а переиспользует кэш (graphify-out/cache). Для CODE-изменений
# граф обновляется через AST (без LLM). Если правил .md — прогони вручную
# `/graphify . --update --obsidian`, чтобы переэкстрактить документацию.
# ============================================================================
set -e
cd "$(dirname "$0")"

# --- resolve python interpreter ---
if [ -f graphify-out/.graphify_python ]; then
    PY=$(cat graphify-out/.graphify_python)
else
    GB=$(which graphify 2>/dev/null || true)
    if [ -n "$GB" ]; then PY=$(head -1 "$GB" | tr -d '#!'); else PY="python3"; fi
    case "$PY" in *[!a-zA-Z0-9/_.-]*) PY="python3" ;; esac
    mkdir -p graphify-out
    "$PY" -c "import sys; open('graphify-out/.graphify_python','w').write(sys.executable)" 2>/dev/null || true
fi
"$PY" -c "import graphify" 2>/dev/null || { echo "[update-graph] graphify не установлен — пропускаю"; exit 0; }

"$PY" - <<'PYEOF'
import json
from pathlib import Path
from graphify.detect import detect, save_manifest
from graphify.extract import collect_files, extract
from graphify.cache import check_semantic_cache
from graphify.build import build_from_json
from graphify.cluster import cluster, score_all
from graphify.analyze import god_nodes, surprising_connections, suggest_questions
from graphify.report import generate
from graphify.export import to_json, to_obsidian, to_canvas
from graphify.wiki import to_wiki
import shutil

ROOT = Path('.').resolve()

def noise(sf):
    sf = sf or ''
    return ('data/static/' in sf or '/build/' in sf or 'graphify-out/' in sf
            or '.claude' in sf or sf.endswith('.min.js')
            or (sf.endswith('.js') and 'main.' in Path(sf).name))

# 1. detect + filter
det = detect(ROOT)
def keep(p): return 'graphify-out/' not in p and '.claude' not in p
for k in list(det['files']): det['files'][k] = [p for p in det['files'][k] if keep(p)]
det['total_files'] = sum(len(v) for v in det['files'].values())

# 2. AST on code
code = []
for f in det['files'].get('code', []):
    code.extend(collect_files(Path(f)) if Path(f).is_dir() else [Path(f)])
ast = extract(code, cache_root=ROOT) if code else {'nodes': [], 'edges': []}

# 3. cached semantic (docs) — no LLM
allf = [p for v in det['files'].values() for p in v]
cn, ce, ch, unc = check_semantic_cache(allf)

# 4. merge + prune
nodes = [n for n in ast['nodes'] if not noise(n.get('source_file'))]
ids = {n['id'] for n in nodes}
for n in cn:
    if n['id'] not in ids and not noise(n.get('source_file')):
        nodes.append(n); ids.add(n['id'])
edges = [e for e in ast['edges'] + ce if e.get('source') in ids and e.get('target') in ids]
hyper = [h for h in ch if all(x in ids for x in h.get('nodes', []))]

# 5. build + cluster
G = build_from_json({'nodes': nodes, 'edges': edges, 'hyperedges': hyper})
comm = cluster(G); coh = score_all(G, comm)

# stable community labels (extend as the project grows)
LABELS = {
    0: 'Protocol Decoders (Flipper port)', 1: 'Key Matching / Frequency / NVS',
    2: 'CC1101 RF Core', 3: 'Gate Control & GSM', 4: 'Flipper Ref: Receiver UI',
    5: 'Decode Architecture & Lessons', 6: 'Web API', 7: 'Frontend: Home/Info',
    8: 'Frontend: Keys Page', 9: 'Frontend: WiFi Page', 10: 'Frontend: Phones Page',
    11: 'Flipper Ref: Signal Gen Info', 12: 'CC1101Manager (header)', 13: 'GSM Manager',
    14: 'Gate Control Module', 15: 'WiFi Manager', 16: 'Storage Service', 17: 'Logger',
}
for c in comm: LABELS.setdefault(c, 'Community ' + str(c))

# 6. outputs (NO html)
q = suggest_questions(G, comm, LABELS)
Path('graphify-out/GRAPH_REPORT.md').write_text(
    generate(G, comm, coh, LABELS, god_nodes(G), surprising_connections(G, comm),
             det, {'input': 0, 'output': 0}, str(ROOT), suggested_questions=q))
to_json(G, comm, 'graphify-out/graph.json')
shutil.rmtree('graphify-out/obsidian', ignore_errors=True)
to_obsidian(G, comm, 'graphify-out/obsidian', community_labels=LABELS, cohesion=coh)
to_canvas(G, comm, 'graphify-out/obsidian/graph.canvas', community_labels=LABELS)
shutil.rmtree('graphify-out/wiki', ignore_errors=True)
to_wiki(G, comm, 'graphify-out/wiki', community_labels=LABELS, cohesion=coh, god_nodes_data=god_nodes(G))
save_manifest(det['files'])
Path('graphify-out/graph.html').unlink(missing_ok=True)
print(f"[update-graph] {G.number_of_nodes()} nodes, {G.number_of_edges()} edges, {len(comm)} communities (no html)")
PYEOF
