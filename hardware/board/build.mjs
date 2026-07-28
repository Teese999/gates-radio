#!/usr/bin/env node
/* =========================================================================
   Сборщик самодостаточной страницы вьюера.

   Читает board.json + viewer.css + viewer.js + vendor/three.min.js
   и склеивает всё в один index.html без единого внешнего запроса.

   Зачем: index.html должен открываться двойным кликом с диска (file://),
   а fetch('board.json') из file:// блокируется CORS — поэтому JSON
   инлайнится прямо в страницу на этапе сборки.

   Правим раскладку только в board.json, потом: node build.mjs
   ========================================================================= */

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const p = (...s) => join(HERE, ...s);

/* ---------- чтение исходников ---------- */

function read(rel, { required = true } = {}) {
  const file = p(rel);
  if (!existsSync(file)) {
    if (required) {
      console.error(`[build] нет файла: ${rel}`);
      process.exit(1);
    }
    return null;
  }
  return readFileSync(file, 'utf8');
}

const boardRaw = read('board.json');
const css = read('viewer.css');
const viewer = read('viewer.js');
const three = read('vendor/three.min.js', { required: false });

let board;
try {
  board = JSON.parse(boardRaw);
} catch (e) {
  console.error('[build] board.json не парсится:', e.message);
  process.exit(1);
}

/* ---------- проверки безопасности инлайна ---------- */

// Внутри <script> последовательность "</script" закрывает тег раньше времени.
for (const [name, text] of [['viewer.js', viewer], ['vendor/three.min.js', three || '']]) {
  if (/<\/script/i.test(text)) {
    console.error(`[build] в ${name} встречается "</script" — инлайн сломает страницу.`);
    process.exit(1);
  }
}

// JSON инлайним с экранированием "<", чтобы никакой текст не закрыл тег.
const boardJson = JSON.stringify(board).replace(/</g, '\\u003c');

const threeBlock = three
  ? `<script>\n${three}\n</script>`
  : `<script>
/* Заглушка: vendor/three.min.js не найден.
   Скачай UMD-сборку three.js (r140–r147) и положи её в vendor/three.min.js:
     curl -L -o vendor/three.min.js https://unpkg.com/three@0.147.0/build/three.min.js
   затем пересобери страницу: node build.mjs */
</script>`;

if (!three) console.warn('[build] ВНИМАНИЕ: vendor/three.min.js отсутствует — собрана заглушка.');

/* ---------- разметка страницы ---------- */

const meta = board.meta || {};

const html = `<!doctype html>
<html lang="ru" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="dark light">
<title>${esc(meta.name || 'Монтажная плата')} — 3D-вьюер</title>
<style>
${css}
</style>
</head>
<body>

<div id="stage"></div>

<div id="ui">

  <!-- верхняя панель -->
  <header class="topbar">
    <div class="brand">
      <b id="brand-title">Монтажная плата</b>
      <span id="brand-sub"></span>
    </div>
    <div class="spacer"></div>
    <div class="btnrow">
      <button id="view-top" title="Клавиша 1">Сверху</button>
      <button id="view-bottom" title="Клавиша 2">Снизу</button>
      <button id="view-iso" title="Клавиша 3">Изометрия</button>
      <button id="view-reset" class="ghost" title="Сброс камеры и подсветки">Сброс</button>
      <button id="theme-toggle" class="ghost">☾ Тёмная</button>
    </div>
  </header>

  <!-- слева: слои и легенда -->
  <aside class="panel panel-left">
    <div class="panel-head">
      <span>Слои и легенда</span>
      <span class="spacer"></span>
      <button class="icon ghost collapse" title="Свернуть">▾</button>
    </div>
    <div class="panel-body">
      <div class="section">
        <h4>Разводка (снизу платы)</h4>
        <label class="check"><input type="checkbox" data-layer="power" checked>
          <i class="sw" style="background:#BA7517"></i><span>Питание</span><small>12 / 5 / 4 / 3.3 В</small></label>
        <label class="check"><input type="checkbox" data-layer="gnd" checked>
          <i class="sw" style="background:#888780"></i><span>Земля</span><small>звезда</small></label>
        <label class="check"><input type="checkbox" data-layer="signal" checked>
          <i class="sw" style="background:linear-gradient(135deg,#7F77DD,#1D9E75)"></i><span>Сигналы</span><small>SPI / UART</small></label>
        <label class="check"><input type="checkbox" data-layer="mains" checked>
          <i class="sw" style="background:#D4353B"></i><span>220 В</span><small>навесом ПОВЕРХ платы</small></label>
      </div>
      <div class="section">
        <h4>Сверху платы</h4>
        <label class="check"><input type="checkbox" data-layer="components" checked>
          <i class="sw" style="background:#378ADD"></i><span>Компоненты</span></label>
        <label class="check"><input type="checkbox" data-layer="zones" checked>
          <i class="sw" style="background:#1D9E75"></i><span>Зоны</span></label>
        <label class="check"><input type="checkbox" data-layer="labels" checked>
          <i class="sw" style="background:#e7ebf2"></i><span>Подписи</span></label>
        <label class="check"><input type="checkbox" id="board-xray">
          <i class="sw" style="background:transparent;border:1px dashed currentColor"></i><span>Просветить плату</span></label>
      </div>
      <div class="section">
        <h4>Вне платы</h4>
        <label class="check"><input type="checkbox" data-layer="enclosure" checked>
          <i class="sw" style="background:rgba(212,53,59,0.35);border:1px dashed #D4353B"></i><span>Корпус и кабели</span><small>стенки и 220 В</small></label>
        <label class="check"><input type="checkbox" data-layer="case">
          <i class="sw" style="background:transparent;border:1px dashed #7c8ea3"></i><span>Корпус (контур)</span><small>160×110×46</small></label>
        <div class="sub-legend">
          <div><i class="sw" style="background:#8A5A2B"></i><span>фаза (L)</span><small>коричневый, через предохранитель и реле</small></div>
          <div><i class="sw" style="background:#3D6FB4"></i><span>ноль (N)</span><small>синий, без коммутации</small></div>
          <div><i class="sw" style="background:#BA7517"></i><span>12 В на плату</span><small>постоянка после БП</small></div>
        </div>
      </div>
      <div class="section" id="legend"></div>
    </div>
  </aside>

  <!-- справа: сборка, списки, информация -->
  <aside class="panel panel-right">
    <div class="panel-head">
      <span>Сборка платы</span>
      <span class="spacer"></span>
      <button class="icon ghost collapse" title="Свернуть">▾</button>
    </div>
    <div class="tabs">
      <button data-tab="steps" class="on">Шаги</button>
      <button data-tab="list">Список</button>
      <button data-tab="info">Инфо</button>
    </div>
    <div class="panel-body">

      <div class="tab-pane on" data-pane="steps">
        <div class="btnrow" style="margin-bottom:10px">
          <button id="step-prev" title="←">Назад</button>
          <button id="step-next" title="→">Вперёд</button>
          <button id="step-clear" class="ghost" title="Esc">Показать всё</button>
        </div>
        <div id="steps"></div>
        <div class="step-detail" id="step-detail" style="display:none"></div>
      </div>

      <div class="tab-pane" data-pane="list">
        <div class="section" style="border:0;padding:0;margin:0">
          <h4>Компоненты</h4>
          <div id="comps"></div>
        </div>
        <div class="section">
          <h4>В корпусе (не на плате)</h4>
          <div id="encl"></div>
        </div>
        <div class="section">
          <h4>Цепи</h4>
          <div id="nets"></div>
        </div>
      </div>

      <div class="tab-pane" data-pane="info">
        <div id="info"></div>
      </div>

    </div>
  </aside>

  <!-- нижняя строка -->
  <div class="hud">
    <span id="hud-hover" class="mono"></span>
    <span id="hud-hint"></span>
    <span class="spacer"></span>
    <span id="hud-stats"></span>
  </div>

</div>

<script>window.BOARD_DATA = ${boardJson};</script>
${threeBlock}
<script>
${viewer}
</script>
</body>
</html>
`;

writeFileSync(p('index.html'), html, 'utf8');

const kb = (n) => (n / 1024).toFixed(0) + ' КБ';
console.log('[build] index.html собран:', kb(Buffer.byteLength(html)));
console.log('[build]   board.json  :', kb(boardRaw.length),
  `(${(board.components || []).length} элементов, ${(board.nets || []).length} цепей, ${(board.steps || []).length} шагов)`);
console.log('[build]   viewer.js   :', kb(viewer.length));
console.log('[build]   viewer.css  :', kb(css.length));
console.log('[build]   three.js    :', three ? kb(three.length) : 'нет (заглушка)');

function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
