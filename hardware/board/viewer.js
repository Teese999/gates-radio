/* =========================================================================
   Умные Ворота — интерактивный 3D-вьюер монтажной платы.

   Источник данных — board.json, который сборщик (build.mjs) инлайнит
   в index.html как window.BOARD_DATA. Библиотека three.js подключается
   как UMD-глобал THREE, орбитальные контролы — свои (см. Orbit).

   Система координат:
     колонка 1..cols -> ось X, ряд 1..rows -> ось Z, вверх -> ось Y.
     x = (col-1)*pitch, z = (row-1)*pitch, плата центрирована в нуле.
   ========================================================================= */

(function () {
  'use strict';

  var DATA = window.BOARD_DATA;
  if (!DATA || !DATA.board) {
    document.body.innerHTML =
      '<div class="fatal">Не найдены данные платы (window.BOARD_DATA).<br>' +
      'Собери страницу заново: <b>node build.mjs</b></div>';
    return;
  }
  if (typeof THREE === 'undefined') {
    document.body.innerHTML =
      '<div class="fatal">Не найдена библиотека three.js.<br>' +
      'Положи UMD-сборку в vendor/three.min.js и пересобери: <b>node build.mjs</b></div>';
    return;
  }

  /* ------------------------------------------------------------------ */
  /*  Константы и справочники                                            */
  /* ------------------------------------------------------------------ */

  var RAMP = {
    blue:   '#378ADD',
    teal:   '#1D9E75',
    purple: '#7F77DD',
    amber:  '#BA7517',
    coral:  '#D85A30',
    red:    '#D4353B',
    gray:   '#888780',
    green:  '#639922'
  };
  var DEFAULT_RAMP = 'gray';

  // Классы цепей: подпись, радиус провода (мм) и «этаж» разводки под платой.
  var NET_CLASS = {
    power:  { label: 'Питание',  radius: 0.78, depth: 4.8 },
    gnd:    { label: 'Земля',    radius: 0.78, depth: 2.6 },
    signal: { label: 'Сигнал',   radius: 0.42, depth: 7.0 },
    mains:  { label: '220 В',    radius: 1.20, depth: 3.6 }
  };
  var NET_CLASS_FALLBACK = { label: 'Прочее', radius: 0.55, depth: 5.6 };

  // 220 В — переменный ток, плюса/минуса нет: жилы красим как в реальном
  // кабеле — фаза (L) коричневая, ноль (N) синяя, вторая коммутируемая
  // фаза «закрыть» (L2) чёрная. Метка — поле conductor у mains-цепи
  // или у связи в корпусе.
  var CONDUCTOR_COLOR = { L: 0x8a5a2b, N: 0x3d6fb4, L2: 0x33363c, PE: 0x86a83d };

  var KIND_LABEL = {
    module: 'Модуль',
    buck: 'Понижайка DC-DC',
    relay: 'Реле',
    transistor: 'Транзистор',
    diode: 'Диод',
    resistor: 'Резистор',
    capacitor: 'Конденсатор',
    terminal: 'Клеммник',
    junction: 'Узел пайки',
    psu: 'Блок питания',
    fuse: 'Держатель предохранителя',
    'terminal-block': 'Клеммная колодка',
    led: 'Светодиод'
  };

  // Как читается поле mount компонента в панели информации
  var MOUNT_LABEL = {
    vertical: 'вертикально, на ребре — в угловой гребёнке'
  };

  var FONT_STACK = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif';

  var THEMES = {
    dark:  { bg: 0x0b0e13, board: 0x1b4430, boardEdge: 0x0d2419, pad: 0xb98f3a, hole: 0x05070a,
             hemiSky: 0x9fb6d0, hemiGround: 0x161b23, ambient: 0.22 },
    light: { bg: 0xe9edf2, board: 0x27633f, boardEdge: 0x123322, pad: 0xd0a44a, hole: 0x141a16,
             hemiSky: 0xffffff, hemiGround: 0xa8b4c2, ambient: 0.34 }
  };

  /* ------------------------------------------------------------------ */
  /*  Геометрия платы                                                    */
  /* ------------------------------------------------------------------ */

  var B = DATA.board;
  var COLS = B.cols || 58;
  var ROWS = B.rows || 38;
  var PITCH = B.pitch || 2.54;
  var THICK = B.thickness || 1.6;

  /* Габарит текстолита берём из sizeMM: у настоящей протоплаты по краям есть
     зелёные поля, а сетка пятачков смещена от угла на gridOffsetMM. Плата
     центрирована в нуле, поэтому смещения считаем от её левого верхнего угла.
     Старый формат (без sizeMM) остаётся рабочим: сетка с полем в полшага. */
  var BOARD_W = (B.sizeMM && B.sizeMM[0]) || COLS * PITCH;
  var BOARD_D = (B.sizeMM && B.sizeMM[1]) || ROWS * PITCH;
  var GRID_OFF = B.gridOffsetMM || [
    (BOARD_W - (COLS - 1) * PITCH) / 2,
    (BOARD_D - (ROWS - 1) * PITCH) / 2
  ];
  var OX = -BOARD_W / 2 + GRID_OFF[0];  // центр пятачка [1,1] в координатах сцены
  var OZ = -BOARD_D / 2 + GRID_OFF[1];
  var TOP = THICK / 2;                  // верхняя поверхность текстолита
  var BOT = -THICK / 2;                 // нижняя поверхность

  function hx(col) { return (col - 1) * PITCH + OX; }
  function hz(row) { return (row - 1) * PITCH + OZ; }

  function rampColor(name) { return RAMP[name] || RAMP[DEFAULT_RAMP]; }
  function rampHex(name) { return parseInt(rampColor(name).slice(1), 16); }
  function netClass(cls) { return NET_CLASS[cls] || NET_CLASS_FALLBACK; }
  /* Цвет цепи: сетевые жилы — по проводнику (как в реальном кабеле), у прочих
     каждая ЛОГИЧЕСКАЯ цепь имеет свой цвет (net.color) — так провода различимы
     при монтаже; ramp остаётся запасным вариантом для старых данных. */
  function netHex(n) {
    var c = n && n['class'] === 'mains' && CONDUCTOR_COLOR[n.conductor];
    if (c) return c;
    if (n && n.color) return parseInt(n.color.slice(1), 16);
    return rampHex(n && n.ramp);
  }
  function netCss(n) {
    return '#' + ('00000' + netHex(n).toString(16)).slice(-6);
  }

  // Индексы для быстрых поисков
  var byComp = {}, byNet = {}, byZone = {}, byEnc = {};
  (DATA.components || []).forEach(function (c) { byComp[c.id] = c; });
  (DATA.nets || []).forEach(function (n) { byNet[n.id] = n; });
  (DATA.zones || []).forEach(function (z) { byZone[z.id] = z; });

  // То, что живёт в корпусе рядом с платой (блок питания, колодки, ввод 220 В)
  var ENC = DATA.enclosure || {};
  var encNodes = ENC.nodes || [];
  var encLinks = ENC.links || [];
  encNodes.forEach(function (n) { byEnc[n.id] = n; });

  /* ------------------------------------------------------------------ */
  /*  Сцена                                                              */
  /* ------------------------------------------------------------------ */

  var theme = 'dark';
  var stage = document.getElementById('stage');

  var renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, window.innerWidth <= 700 ? 1.6 : 2));
  renderer.outputEncoding = THREE.sRGBEncoding;
  stage.appendChild(renderer.domElement);

  var scene = new THREE.Scene();
  // Узкий угол: перспектива мягче, плата читается ближе к чертёжной проекции
  var camera = new THREE.PerspectiveCamera(30, 1, 0.5, 6000);

  var hemi = new THREE.HemisphereLight(0xffffff, 0x222222, 0.55);
  scene.add(hemi);
  var ambient = new THREE.AmbientLight(0xffffff, 0.22);
  scene.add(ambient);
  var key = new THREE.DirectionalLight(0xffffff, 0.85);
  key.position.set(90, 210, 130);
  scene.add(key);
  var rim = new THREE.DirectionalLight(0xcfe0ff, 0.30);
  rim.position.set(-150, 120, -110);
  scene.add(rim);
  var under = new THREE.DirectionalLight(0xbcd0ff, 0.40);   // подсветка снизу — для вида на разводку
  under.position.set(-40, -200, 60);
  scene.add(under);

  var root = new THREE.Group();
  scene.add(root);

  var gBoard = new THREE.Group();
  var gZones = new THREE.Group();
  var gComponents = new THREE.Group();
  var gLabels = new THREE.Group();
  var gWires = new THREE.Group();
  var gEnclosure = new THREE.Group();   // узлы в корпусе + кабели к плате
  root.add(gBoard, gZones, gWires, gComponents, gLabels, gEnclosure);

  // Подгруппы проводов по классам цепей — их и переключают чекбоксы слоёв.
  var wireGroups = {};
  Object.keys(NET_CLASS).concat(['other']).forEach(function (k) {
    wireGroups[k] = new THREE.Group();
    gWires.add(wireGroups[k]);
  });

  /* ------------------------------------------------------------------ */
  /*  Реестр объектов: подсветка/приглушение и пикинг                    */
  /* ------------------------------------------------------------------ */

  var shaded = [];       // всё, что умеет приглушаться
  var pickables = [];    // всё, что ловит клик
  var labelSprites = []; // текстовые спрайты — им подгоняется масштаб под зум

  function register(obj, meta, opts) {
    opts = opts || {};
    obj.userData.meta = meta;
    var m = obj.material;
    if (m) {
      obj.userData.base = {
        opacity: m.opacity,
        transparent: m.transparent,
        depthWrite: m.depthWrite !== false,
        emissive: m.emissive ? m.emissive.getHex() : 0,
        hi: m.color ? m.color.clone().multiplyScalar(0.42).getHex() : 0x000000
      };
      shaded.push(obj);
    }
    if (opts.pick) pickables.push(obj);
    return obj;
  }

  /* ------------------------------------------------------------------ */
  /*  Текстовые подписи (canvas -> спрайт)                               */
  /* ------------------------------------------------------------------ */

  function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  function makeLabel(text, opt) {
    opt = opt || {};
    var fs = 58, pad = 20;
    var cv = document.createElement('canvas');
    var ctx = cv.getContext('2d');
    ctx.font = '600 ' + fs + 'px ' + FONT_STACK;
    var w = Math.ceil(ctx.measureText(text).width) + pad * 2;
    var h = fs + pad * 2;
    cv.width = w; cv.height = h;
    ctx.font = '600 ' + fs + 'px ' + FONT_STACK;   // после resize контекст сбрасывается
    ctx.textBaseline = 'middle';
    roundRect(ctx, 2, 2, w - 4, h - 4, 18);
    ctx.fillStyle = opt.bg || 'rgba(9,12,17,0.80)';
    ctx.fill();
    ctx.lineWidth = 3;
    ctx.strokeStyle = opt.border || 'rgba(255,255,255,0.22)';
    ctx.stroke();
    ctx.fillStyle = opt.color || '#ffffff';
    ctx.fillText(text, pad, h / 2 + 2);

    var tex = new THREE.CanvasTexture(cv);
    tex.encoding = THREE.sRGBEncoding;
    tex.anisotropy = 4;
    var mat = new THREE.SpriteMaterial({ map: tex, transparent: true, depthTest: opt.depthTest !== false });
    var sp = new THREE.Sprite(mat);
    var H = opt.size || 4.4;
    sp.scale.set(H * w / h, H, 1);
    sp.userData.baseScale = { x: H * w / h, y: H };
    labelSprites.push(sp);
    return sp;
  }

  /* Подписи держим примерно постоянного экранного размера: при отдалении они
     иначе превращаются в нечитаемые пиксели, при приближении — в баннеры. */
  var LABEL_REF = 0;
  var labelScale = 0;
  function updateLabelScale() {
    if (!LABEL_REF) return;
    var k = Math.max(0.72, Math.min(1.8, orbit.radius / LABEL_REF));
    if (Math.abs(k - labelScale) < 0.01) return;
    labelScale = k;
    for (var i = 0; i < labelSprites.length; i++) {
      var s = labelSprites[i], b = s.userData.baseScale;
      s.scale.set(b.x * k, b.y * k, 1);
    }
  }

  /* ------------------------------------------------------------------ */
  /*  1. Текстолит и сетка отверстий                                     */
  /* ------------------------------------------------------------------ */

  var boardMat = new THREE.MeshStandardMaterial({
    color: THEMES.dark.board, roughness: 0.78, metalness: 0.06
  });
  var boardMesh = new THREE.Mesh(new THREE.BoxGeometry(BOARD_W, THICK, BOARD_D), boardMat);
  gBoard.add(boardMesh);

  var boardEdges = new THREE.LineSegments(
    new THREE.EdgesGeometry(boardMesh.geometry),
    new THREE.LineBasicMaterial({ color: THEMES.dark.boardEdge, transparent: true, opacity: 0.75 })
  );
  gBoard.add(boardEdges);

  // Отверстия рисуем инстансами: 4 InstancedMesh вместо 2204*4 мешей.
  (function buildHoles() {
    var n = COLS * ROWS;
    var ringGeo = new THREE.RingGeometry(0.60, 0.96, 12);
    var holeGeo = new THREE.CircleGeometry(0.62, 10);
    var padMat = new THREE.MeshStandardMaterial({
      color: THEMES.dark.pad, roughness: 0.42, metalness: 0.65, side: THREE.DoubleSide
    });
    var holeMat = new THREE.MeshBasicMaterial({ color: THEMES.dark.hole, side: THREE.DoubleSide });

    var padTop = new THREE.InstancedMesh(ringGeo, padMat, n);
    var padBot = new THREE.InstancedMesh(ringGeo, padMat, n);
    var holTop = new THREE.InstancedMesh(holeGeo, holeMat, n);
    var holBot = new THREE.InstancedMesh(holeGeo, holeMat, n);

    var mUp = new THREE.Matrix4().makeRotationX(-Math.PI / 2);
    var mDn = new THREE.Matrix4().makeRotationX(Math.PI / 2);
    var m = new THREE.Matrix4();
    var i = 0;
    for (var r = 1; r <= ROWS; r++) {
      for (var c = 1; c <= COLS; c++) {
        var x = hx(c), z = hz(r);
        m.copy(mUp).setPosition(x, TOP + 0.012, z); padTop.setMatrixAt(i, m);
        m.copy(mUp).setPosition(x, TOP + 0.020, z); holTop.setMatrixAt(i, m);
        m.copy(mDn).setPosition(x, BOT - 0.012, z); padBot.setMatrixAt(i, m);
        m.copy(mDn).setPosition(x, BOT - 0.020, z); holBot.setMatrixAt(i, m);
        i++;
      }
    }
    [padTop, padBot, holTop, holBot].forEach(function (im) {
      im.instanceMatrix.needsUpdate = true;
      im.frustumCulled = false;
      gBoard.add(im);
    });
    boardMesh.userData.padMat = padMat;
    boardMesh.userData.holeMat = holeMat;

    /* Краевые полосы (board.edgeStrips): по длинным краям платы пятачки
       ОВАЛЬНЫЕ — вытянуты поперёк кромки (сверено с фото реальной платы) и
       электрически НЕЗАВИСИМЫ, как обычные отверстия. Рисуем овальное кольцо
       поверх штатного круглого пятачка с обеих сторон платы. */
    var es = B.edgeStrips;
    if (es) {
      var ovalGeo = new THREE.RingGeometry(0.60, 0.96, 14);
      var addOval = function (x, z, acrossRows) {
        [[TOP + 0.016, -Math.PI / 2], [BOT - 0.016, Math.PI / 2]].forEach(function (s) {
          var o = new THREE.Mesh(ovalGeo, padMat);
          o.rotation.x = s[1];
          // после поворота локальная Y ложится вдоль мировой Z (рядов)
          o.scale.set(acrossRows ? 1 : 1.75, acrossRows ? 1.75 : 1, 1);
          o.position.set(x, s[0], z);
          gBoard.add(o);
        });
      };
      (es.rows || []).forEach(function (r) {
        for (var c = 1; c <= COLS; c++) addOval(hx(c), hz(r), true);
      });
      (es.cols || []).forEach(function (c2) {
        for (var r2 = 1; r2 <= ROWS; r2++) addOval(hx(c2), hz(r2), false);
      });
    }
  })();

  /* Крепёжные отверстия. Новый формат — миллиметры от угла платы
     ({units,diameter,positions}), они лежат в зелёном поле и посадочных мест
     не занимают; старый формат (массив [колонка, ряд]) тоже понимаем. */
  (function buildMountingHoles() {
    var mh = DATA.board.mountingHoles;
    if (!mh) return;
    var byGrid = Array.isArray(mh);
    var list = byGrid ? mh : (mh.positions || []);
    var r = ((!byGrid && mh.diameter) || 3.2) / 2;
    var wallMat = new THREE.MeshStandardMaterial({
      color: 0x0e1116, roughness: 0.85, side: THREE.DoubleSide
    });
    var faceMat = new THREE.MeshBasicMaterial({ color: 0x0e1116, side: THREE.DoubleSide });
    var rimMat = new THREE.MeshStandardMaterial({
      color: 0xa9b0b8, roughness: 0.55, metalness: 0.25, side: THREE.DoubleSide
    });
    var wallGeo = new THREE.CylinderGeometry(r, r, THICK + 0.06, 22, 1, true);
    var faceGeo = new THREE.CircleGeometry(r, 22);
    var rimGeo = new THREE.RingGeometry(r, r + 0.45, 22);
    list.forEach(function (h) {
      var x = byGrid ? hx(h[0]) : -BOARD_W / 2 + h[0];
      var z = byGrid ? hz(h[1]) : -BOARD_D / 2 + h[1];
      var wall = new THREE.Mesh(wallGeo, wallMat);
      wall.position.set(x, 0, z);
      gBoard.add(wall);
      [[TOP + 0.02, -Math.PI / 2], [BOT - 0.02, Math.PI / 2]].forEach(function (s) {
        var face = new THREE.Mesh(faceGeo, faceMat);
        face.rotation.x = s[1];
        face.position.set(x, s[0], z);
        gBoard.add(face);
        var rim = new THREE.Mesh(rimGeo, rimMat);
        rim.rotation.x = s[1];
        rim.position.set(x, s[0] + (s[0] > 0 ? 0.004 : -0.004), z);
        gBoard.add(rim);
      });
    });
  })();

  /* ------------------------------------------------------------------ */
  /*  2. Зоны                                                            */
  /* ------------------------------------------------------------------ */

  // Штриховка для keepout-зоны — генерируется на canvas, без внешних картинок.
  function hatchTexture(color) {
    var s = 64;
    var cv = document.createElement('canvas');
    cv.width = cv.height = s;
    var ctx = cv.getContext('2d');
    ctx.fillStyle = color;
    ctx.globalAlpha = 0.22;
    ctx.fillRect(0, 0, s, s);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = color;
    ctx.lineWidth = 9;
    for (var i = -s; i < s * 2; i += 22) {
      ctx.beginPath();
      ctx.moveTo(i, 0);
      ctx.lineTo(i + s, s);
      ctx.stroke();
    }
    var tex = new THREE.CanvasTexture(cv);
    tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
    tex.encoding = THREE.sRGBEncoding;
    return tex;
  }

  function rectMetrics(rect) {
    var c1 = Math.min(rect[0], rect[2]), c2 = Math.max(rect[0], rect[2]);
    var r1 = Math.min(rect[1], rect[3]), r2 = Math.max(rect[1], rect[3]);
    return {
      c1: c1, r1: r1, c2: c2, r2: r2,
      w: (c2 - c1 + 1) * PITCH,
      d: (r2 - r1 + 1) * PITCH,
      cx: (hx(c1) + hx(c2)) / 2,
      cz: (hz(r1) + hz(r2)) / 2
    };
  }

  (DATA.zones || []).forEach(function (z, idx) {
    if (!z.rect) return;
    var g = rectMetrics(z.rect);
    var col = rampHex(z.ramp);
    var keepout = z.kind === 'keepout';
    var clearance = z.kind === 'clearance';
    var clearH = z.height || 5;         // потолок коридора, мм
    var y = TOP + 0.06 + idx * 0.035;   // разные высоты, чтобы плоскости не мерцали

    if (clearance) {
      /* Коридор под кабель (USB-C) — не плоская заливка, а низкая
         полупрозрачная подложка: сразу видно, до какой высоты сюда ничего
         не ставить. Рамка пунктирная — это ограничение, а не деталь. */
      var slab = new THREE.Mesh(
        new THREE.BoxGeometry(g.w, clearH, g.d),
        new THREE.MeshStandardMaterial({
          color: col, roughness: 0.9, metalness: 0.0,
          transparent: true, opacity: 0.22, depthWrite: false
        })
      );
      slab.position.set(g.cx, TOP + clearH / 2, g.cz);
      gZones.add(slab);
      register(slab, { kind: 'zone', id: z.id }, { pick: true });

      var dash = new THREE.LineSegments(
        new THREE.EdgesGeometry(slab.geometry),
        new THREE.LineDashedMaterial({
          color: col, dashSize: 2.4, gapSize: 1.8, transparent: true, opacity: 0.9
        })
      );
      dash.position.copy(slab.position);
      dash.computeLineDistances();
      gZones.add(dash);
      register(dash, { kind: 'zone', id: z.id });
    } else {
      var matOpt = {
        color: keepout ? 0xffffff : col,
        transparent: true,
        opacity: keepout ? 0.42 : 0.15,
        depthWrite: false,
        side: THREE.DoubleSide
      };
      if (keepout) {
        matOpt.map = hatchTexture(rampColor(z.ramp));
        matOpt.map.repeat.set(g.w / 12, g.d / 12);
      }
      var plane = new THREE.Mesh(new THREE.PlaneGeometry(g.w, g.d), new THREE.MeshBasicMaterial(matOpt));
      plane.rotation.x = -Math.PI / 2;
      plane.position.set(g.cx, y, g.cz);
      gZones.add(plane);
      register(plane, { kind: 'zone', id: z.id }, { pick: true });

      // рамка зоны
      var hw = g.w / 2, hd = g.d / 2;
      var pts = [
        new THREE.Vector3(-hw, 0, -hd), new THREE.Vector3(hw, 0, -hd),
        new THREE.Vector3(hw, 0, hd), new THREE.Vector3(-hw, 0, hd),
        new THREE.Vector3(-hw, 0, -hd)
      ];
      var border = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(pts),
        new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: keepout ? 0.95 : 0.55 })
      );
      border.position.set(g.cx, y + 0.01, g.cz);
      gZones.add(border);
      register(border, { kind: 'zone', id: z.id });
    }

    var text = keepout ? '⚠ ' + z.label : (clearance ? z.label + ' · ≤' + clearH + ' мм' : z.label);
    var lab = makeLabel(text, {
      size: keepout ? 5.0 : (clearance ? 3.4 : 4.2),
      color: keepout ? '#ffd7d8' : '#e9edf5',
      border: 'rgba(' + [col >> 16 & 255, col >> 8 & 255, col & 255].join(',') + ',0.85)',
      bg: keepout ? 'rgba(60,10,12,0.88)' : 'rgba(9,12,17,0.78)'
    });
    // подписи зон висят выше самых высоких корпусов (SIM800L на ребре — 25 мм,
    // конденсатор — 17 мм), иначе их закрывают модули; подпись коридора —
    // низко, у самого коридора, чтобы читалась вместе с ним
    lab.position.set(g.cx, TOP + (keepout ? 31 : (clearance ? clearH + 5 : 26)), g.cz);
    gZones.add(lab);
    register(lab, { kind: 'zone', id: z.id });
  });

  /* ------------------------------------------------------------------ */
  /*  2b. Координатная линейка: колонки и ряды через каждые 5 отверстий  */
  /*      Подписи висят ЗА краями платы — видны и сверху, и снизу.       */
  /* ------------------------------------------------------------------ */

  (function buildRuler() {
    var opt = { size: 2.6, color: '#9aa4b2', bg: 'rgba(9,12,17,0.55)' };
    function rl(text, x, z) {
      var s = makeLabel(String(text), opt);
      s.position.set(x, 0, z);
      gLabels.add(s);
      register(s, { kind: 'label' });
    }
    for (var c = 5; c <= COLS - 3; c += 5) {
      rl(c, hx(c), -BOARD_D / 2 - 3.5);
      rl(c, hx(c), BOARD_D / 2 + 3.5);
    }
    for (var r = 5; r <= ROWS - 3; r += 5) {
      rl(r, -BOARD_W / 2 - 3.5, hz(r));
      rl(r, BOARD_W / 2 + 3.5, hz(r));
    }
  })();

  /* ------------------------------------------------------------------ */
  /*  3. Компоненты                                                      */
  /* ------------------------------------------------------------------ */

  function compMetrics(comp) {
    var body = comp.body || [1, 1, 1, 1];
    var g = rectMetrics(body);
    g.h = Math.max(comp.height || 3, 0.8);
    return g;
  }

  /* ---------- «узнаваемые» корпуса ---------------------------------- *
   * Каждый тип компонента собирается из примитивов так, чтобы силуэт
   * совпадал с тем, что человек держит в руках при пайке. Габариты —
   * из body/height в board.json, силуэт строится внутри этого объёма.
   * ВАЖНО: материалы не разделяются между разными компонентами — иначе
   * приглушение (opacity) одного компонента заденет другой.            */

  var COL_SILVER = 0xc4c9cf;    // металл экранов и колпачков
  var COL_GOLD = 0xd6b45a;      // позолота штырьков
  var COL_LEAD = 0xb9bec7;      // лужёные выводы
  var COL_DARK_PCB = 0x14171c;  // чёрный текстолит

  function matStd(color, o) {
    o = o || {};
    var transparent = o.opacity !== undefined && o.opacity < 1;
    return new THREE.MeshStandardMaterial({
      color: color,
      roughness: o.rough !== undefined ? o.rough : 0.6,
      metalness: o.metal !== undefined ? o.metal : 0.1,
      map: o.map || null,
      transparent: transparent,
      opacity: o.opacity !== undefined ? o.opacity : 1,
      depthWrite: !transparent,
      side: o.side || THREE.FrontSide
    });
  }

  function boxMesh(w, h, d, mat) { return new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat); }
  function cylMesh(r, h, mat, seg) { return new THREE.Mesh(new THREE.CylinderGeometry(r, r, h, seg || 16), mat); }

  function canvasTex(w, h, draw) {
    var cv = document.createElement('canvas');
    cv.width = w; cv.height = h;
    var ctx = cv.getContext('2d');
    draw(ctx, w, h);
    var tex = new THREE.CanvasTexture(cv);
    tex.encoding = THREE.sRGBEncoding;
    tex.anisotropy = 4;
    return tex;
  }

  // Меандр печатной антенны ESP32: золотая дорожка на тёмной площадке
  function meanderTex() {
    return canvasTex(128, 64, function (ctx, w, h) {
      ctx.fillStyle = '#1a1d16';
      ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = '#c8a642';
      ctx.lineWidth = 6;
      ctx.lineCap = 'square';
      ctx.beginPath();
      var x = 10, top = 10, bot = h - 10, up = true;
      ctx.moveTo(x, bot);
      while (x < w - 10) {
        ctx.lineTo(x, up ? top : bot);
        x += 13;
        ctx.lineTo(x, up ? top : bot);
        up = !up;
      }
      ctx.stroke();
    });
  }

  // Табличка с текстом (маркировка реле, блока питания)
  function textTex(lines, opt) {
    opt = opt || {};
    return canvasTex(opt.w || 256, opt.h || 128, function (ctx, w, h) {
      ctx.fillStyle = opt.bg || '#274b8f';
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = opt.fg || '#e8ecf2';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      var fs = opt.fs || 30;
      ctx.font = '600 ' + fs + 'px ' + FONT_STACK;
      lines.forEach(function (t, i) {
        ctx.fillText(t, w / 2, h / 2 + (i - (lines.length - 1) / 2) * fs * 1.3);
      });
    });
  }

  // Крышка электролита с крестовой насечкой (клапан)
  function capTopTex() {
    return canvasTex(64, 64, function (ctx, w, h) {
      ctx.fillStyle = '#b9bec4';
      ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = '#6a6f75';
      ctx.lineWidth = 5;
      ctx.beginPath();
      ctx.moveTo(w / 2, 8); ctx.lineTo(w / 2, h - 8);
      ctx.moveTo(8, h / 2); ctx.lineTo(w - 8, h / 2);
      ctx.stroke();
    });
  }

  // Развёртка боковины электролита: чёрный корпус, светлая полоса минуса на шве (u=0)
  function capStripeTex() {
    return canvasTex(256, 128, function (ctx, w, h) {
      ctx.fillStyle = '#1d2126';
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = '#c9ced4';
      ctx.fillRect(0, 0, w * 0.08, h);
      ctx.fillRect(w * 0.92, 0, w * 0.08, h);
      ctx.fillStyle = '#2a2f35';
      for (var y = 16; y < h - 8; y += 30) {   // штрихи «−» на полосе
        ctx.fillRect(3, y, w * 0.08 - 6, 7);
        ctx.fillRect(w * 0.92 + 3, y, w * 0.08 - 6, 7);
      }
    });
  }

  // Кольца номинала резистора по надписи в label
  function resistorBands(label) {
    var m = /(\d+)\s*к/i.exec(label || '');
    var k = m ? parseInt(m[1], 10) : 0;
    var table = {
      1:  [0x7a4a1e, 0x20242a, 0xc23b34],   // коричневый-чёрный-красный
      10: [0x7a4a1e, 0x20242a, 0xd07a1f],   // коричневый-чёрный-оранжевый
      20: [0xc23b34, 0x20242a, 0xd07a1f]    // красный-чёрный-оранжевый
    };
    return (table[k] || table[1]).concat(0xc9a44a);   // + золото (допуск 5%)
  }

  function pinHolePos(comp, name) {
    var f = null;
    (comp.pins || []).forEach(function (p) { if (!f && p.name === name && p.hole) f = p; });
    return f ? new THREE.Vector3(hx(f.hole[0]), TOP, hz(f.hole[1])) : null;
  }

  function genericBox(comp, g, y0, add) {
    var body = boxMesh(g.w, Math.max(g.h, 1), g.d, matStd(rampHex(comp.ramp), { rough: 0.6 }));
    body.position.set(g.cx, y0 + g.h / 2, g.cz);
    add(body);
    var edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(body.geometry),
      new THREE.LineBasicMaterial({ color: 0x0b0e13, transparent: true, opacity: 0.45 })
    );
    edges.position.copy(body.position);
    add(edges, false);
  }

  /* --- ESP32 DevKit: чёрная PCB, банка WROOM, меандр антенны, USB, штырьки --- */
  function buildEsp32(comp, g, y0, add) {
    var pcbH = 1.8;
    var pcb = boxMesh(g.w, pcbH, g.d, matStd(COL_DARK_PCB, { rough: 0.7 }));
    pcb.position.set(g.cx, y0 + pcbH / 2, g.cz);
    add(pcb);
    var top = y0 + pcbH;

    // экран WROOM: корпус + чуть меньшая крышка = «скругление»
    var shW = g.w * 0.40, shD = g.d * 0.64, shH = 3.0;
    var shX = g.cx + g.w * 0.14;
    var shield = boxMesh(shW, shH, shD, matStd(COL_SILVER, { rough: 0.3, metal: 0.75 }));
    shield.position.set(shX, top + shH / 2, g.cz);
    add(shield);
    var lid = boxMesh(shW * 0.9, 0.7, shD * 0.88, matStd(0xd4d9de, { rough: 0.25, metal: 0.8 }));
    lid.position.set(shX, top + shH + 0.35, g.cz);
    add(lid, false);

    // площадка печатной антенны на правом торце (USB смотрит влево)
    var antW = g.w * 0.14;
    var pad = boxMesh(antW, 0.5, shD, matStd(0x22261e, { rough: 0.8 }));
    pad.position.set(g.cx + g.w / 2 - antW / 2 - 0.6, top + 0.25, g.cz);
    add(pad, false);
    var meander = new THREE.Mesh(
      new THREE.PlaneGeometry(antW - 0.6, shD - 0.8),
      matStd(0xffffff, { rough: 0.8, map: meanderTex() })
    );
    meander.rotation.x = -Math.PI / 2;
    meander.position.set(pad.position.x, top + 0.58, g.cz);
    add(meander, false);

    // USB-разъём: если в данных есть usb {at,to} — рисуем реальный, с вылетом
    // за край модуля в свободный коридор; иначе — намёк на левом торце
    if (comp.usb && comp.usb.at && comp.usb.to) {
      addUsbC(comp, top, add);
    } else {
      var usb = boxMesh(g.w * 0.11, 2.0, g.d * 0.30, matStd(0xd0d5da, { rough: 0.25, metal: 0.85 }));
      usb.position.set(g.cx - g.w / 2 + g.w * 0.055 + 0.4, top + 1.0, g.cz);
      add(usb);
    }

    // пара микросхем между USB и экраном
    var chipMat = matStd(0x1b1e24, { rough: 0.5 });
    var chip1 = boxMesh(4.6, 0.9, 4.6, chipMat);
    chip1.position.set(g.cx - g.w * 0.20, top + 0.45, g.cz - g.d * 0.10);
    add(chip1, false);
    var chip2 = boxMesh(3.2, 0.9, 3.2, chipMat);
    chip2.position.set(g.cx - g.w * 0.20, top + 0.45, g.cz + g.d * 0.16);
    add(chip2, false);

    // два ряда штырьков по длинным краям — одним InstancedMesh
    var n = 19;
    var hdr = new THREE.InstancedMesh(
      new THREE.CylinderGeometry(0.32, 0.32, 2.4, 6),
      matStd(COL_GOLD, { rough: 0.35, metal: 0.8 }), n * 2);
    var m = new THREE.Matrix4();
    for (var i = 0; i < n; i++) {
      var lx = g.cx - g.w / 2 + 2.2 + i * ((g.w - 4.4) / (n - 1));
      m.setPosition(lx, top + 1.2, g.cz - g.d / 2 + 1.1); hdr.setMatrixAt(i, m);
      m.setPosition(lx, top + 1.2, g.cz + g.d / 2 - 1.1); hdr.setMatrixAt(n + i, m);
    }
    hdr.instanceMatrix.needsUpdate = true;
    hdr.frustumCulled = false;
    add(hdr, false);
  }

  /* --- Разъём USB-C модуля: торчит за край корпуса ESP32 в свободный
     коридор, по вектору usb.at → usb.to из board.json. Габарит настоящего
     разъёма: 9 мм в ширину, ~3 мм в высоту, вылет — из данных (~7 мм).
     topY — верх платы модуля, разъём стоит прямо на ней. --- */
  function addUsbC(comp, topY, add) {
    var u = comp.usb;
    var a = new THREE.Vector3(hx(u.at[0]), topY, hz(u.at[1]));
    var b = new THREE.Vector3(hx(u.to[0]), topY, hz(u.to[1]));
    var dir = new THREE.Vector3().subVectors(b, a);
    dir.y = 0;
    if (!dir.length()) return;
    dir.normalize();

    var H = 3.0;                                  // высота корпуса разъёма
    var base = a.clone().addScaledVector(dir, -2.0);   // хвост заходит на плату модуля
    var tip = b.clone();
    var shell = spanBox(
      base.clone().setY(topY + H / 2), tip.clone().setY(topY + H / 2),
      9.0, H, matStd(0xc9ced4, { rough: 0.26, metal: 0.85 })
    );
    add(shell);

    // тёмный «рот» разъёма на торце — по нему видно, куда втыкается кабель
    var mouthC = tip.clone().addScaledVector(dir, -0.6).setY(topY + H / 2);
    var mouth = spanBox(
      mouthC, mouthC.clone().addScaledVector(dir, 1.0),
      6.6, 1.7, matStd(0x0e1116, { rough: 0.8 })
    );
    add(mouth, false);

    var lab = makeLabel('USB-C', { size: 2.8, color: '#cfe0ff' });
    lab.position.set(tip.x, topY + H + 4.2, tip.z);
    lab.visible = false;                          // как у мелочи — по наведению/выбору
    gLabels.add(lab);
    register(lab, { kind: 'label', compId: comp.id, minor: true });
  }

  /* --- CC1101: зелёная плата, чип, кварц (SMA рисует addAntenna) --- */
  function buildCc1101(comp, g, y0, add) {
    var pcbH = 1.6;
    var pcb = boxMesh(g.w, pcbH, g.d, matStd(0x1e6b3d, { rough: 0.65 }));
    pcb.position.set(g.cx, y0 + pcbH / 2, g.cz);
    add(pcb);
    var top = y0 + pcbH;
    var chip = boxMesh(4.2, 1.0, 4.2, matStd(0x1b1e24, { rough: 0.5 }));
    chip.position.set(g.cx - g.w * 0.08, top + 0.5, g.cz);
    add(chip);
    var xtal = boxMesh(3.4, 1.0, 1.8, matStd(COL_SILVER, { rough: 0.3, metal: 0.8 }));
    xtal.position.set(g.cx - g.w * 0.08, top + 0.5, g.cz + g.d * 0.26);
    add(xtal, false);
    var c2 = boxMesh(2.0, 0.8, 2.0, matStd(0x1b1e24, { rough: 0.5 }));
    c2.position.set(g.cx + g.w * 0.14, top + 0.4, g.cz - g.d * 0.2);
    add(c2, false);
  }

  /* --- SIM800L, исполнение НА РЕБРЕ (mount:"vertical") ------------------
     У пинов — угловая гребёнка PBS-R (низкий блок с горизонтальными
     гнёздами), из неё модуль поднимается вертикально на всю height.
     Смысл переделки: держатель симки уезжает на боковую грань и остаётся
     доступен сверху — поэтому он на лицевой стороне платы, вместе с IPX,
     а экран модема — на изнанке, со стороны гребёнки. --- */
  function buildSim800Vertical(comp, g, y0, add) {
    var pins = (comp.pins || []).filter(function (p) { return p.hole; });
    var xs = pins.map(function (p) { return hx(p.hole[0]); });
    var pinCx = xs.length ? (Math.min.apply(null, xs) + Math.max.apply(null, xs)) / 2 : g.cx;
    var pinSpan = xs.length ? (Math.max.apply(null, xs) - Math.min.apply(null, xs)) : g.w * 0.5;
    var pinZ = pins.length ? hz(pins[0].hole[1]) : g.cz;

    // 1. Угловая гребёнка: чёрный блок над пинами, гнёзда смотрят горизонтально.
    // Блок сдвинут назад от ряда пинов — вперёд уходят гнёзда, как у PBS-R,
    // иначе вся сборка вылезает из посадочного места в сторону соседей.
    var hdrH = 5.0, hdrD = 4.0, hdrZ = pinZ - 1.4;
    var hdr = boxMesh(pinSpan + 3.4, hdrH, hdrD, matStd(0x191c21, { rough: 0.85 }));
    hdr.position.set(pinCx, y0 + hdrH / 2, hdrZ);
    add(hdr);
    var socketMat = matStd(0x08090c, { rough: 0.9 });
    var contactMat = matStd(COL_GOLD, { rough: 0.35, metal: 0.85 });
    var pinY = y0 + hdrH * 0.62;

    // 2. Плата модуля — вертикально, вынесена на длину горизонтальных пинов
    var plateT = 1.6;
    var plateZ = hdrZ + hdrD / 2 + 1.6 + plateT / 2;   // вынос на длину горизонтальных пинов
    var plateW = Math.max(g.w - 1.4, 8);
    var plateBottom = y0 + hdrH * 0.5;
    var plateH = Math.max(g.h - (plateBottom - y0), 12);
    var plateCy = plateBottom + plateH / 2;
    var pcb = boxMesh(plateW, plateH, plateT, matStd(0xa32530, { rough: 0.6 }));
    pcb.position.set(g.cx, plateCy, plateZ);
    add(pcb);

    // горизонтальные штырьки модуля: из платы в гнёзда гребёнки
    pins.forEach(function (p) {
      var px = hx(p.hole[0]);
      var sock = cylMesh(0.85, 1.2, socketMat, 8);
      sock.rotation.x = Math.PI / 2;
      sock.position.set(px, pinY, hdrZ + hdrD / 2);
      add(sock, false);
      add(spanCylinder(
        new THREE.Vector3(px, pinY, hdrZ + hdrD / 2 - 0.6),
        new THREE.Vector3(px, pinY, plateZ - plateT / 2),
        0.32, contactMat, 6), false);
    });

    // 3. Экран модема — на изнанке (сторона гребёнки)
    var shield = boxMesh(plateW * 0.58, plateH * 0.40, 1.4,
      matStd(COL_SILVER, { rough: 0.3, metal: 0.75 }));
    shield.position.set(g.cx + plateW * 0.10, plateCy + plateH * 0.10, plateZ - plateT / 2 - 0.7);
    add(shield);

    // 4. Держатель симки — на лицевой грани, лоток с прорезью под карту
    var simW = plateW * 0.66, simH = plateH * 0.40;
    var simCy = plateCy - plateH * 0.16;
    var sim = boxMesh(simW, simH, 1.5, matStd(0xcfd4d9, { rough: 0.32, metal: 0.7 }));
    sim.position.set(g.cx, simCy, plateZ + plateT / 2 + 0.75);
    add(sim);
    var slot = boxMesh(simW * 0.86, 1.0, 0.6, matStd(0x0d0f12, { rough: 0.85 }));
    slot.position.set(g.cx, simCy - simH / 2 + 1.1, plateZ + plateT / 2 + 1.3);
    add(slot, false);
    var card = boxMesh(simW * 0.6, simH * 0.5, 0.5, matStd(0xd9b13c, { rough: 0.5, metal: 0.35 }));
    card.position.set(g.cx, simCy + simH * 0.08, plateZ + plateT / 2 + 1.55);
    add(card, false);
    var simLab = makeLabel('держатель SIM', { size: 2.8, color: '#9fd8c2' });
    simLab.position.set(g.cx, simCy, plateZ + 9);
    simLab.visible = false;                 // показываем по наведению/выбору модуля
    gLabels.add(simLab);
    register(simLab, { kind: 'label', compId: comp.id, minor: true });

    // 5. Жёлтый IPX — на лицевой грани, на высоте, откуда уходит пигтейл
    var ipxY = Math.min(plateBottom + plateH - 2.5, y0 + Math.max(g.h * 0.55, 3));
    var ipxX = comp.antenna && comp.antenna.from
      ? Math.max(hx(comp.antenna.from[0]) + 1.4, g.cx - plateW / 2 + 1.6)
      : g.cx - plateW / 2 + 1.6;
    var ipx = boxMesh(2.4, 2.4, 1.5, matStd(0xd9b13c, { rough: 0.45, metal: 0.3 }));
    ipx.position.set(ipxX, ipxY, plateZ + plateT / 2 + 0.75);
    add(ipx, false);

    // 6. Светодиод NET у верхнего края
    var led = boxMesh(1.6, 1.0, 0.8, matStd(0x2f6fd0, { rough: 0.4 }));
    led.position.set(g.cx + plateW * 0.32, plateBottom + plateH - 2.2, plateZ + plateT / 2 + 0.4);
    add(led, false);
  }

  /* --- SIM800L: красная плата, экран, жёлтый IPX, намёк на SIM-держатель --- */
  function buildSim800(comp, g, y0, add) {
    if (comp.mount === 'vertical') return buildSim800Vertical(comp, g, y0, add);
    var pcbH = 1.6;
    var pcb = boxMesh(g.w, pcbH, g.d, matStd(0xa32530, { rough: 0.6 }));
    pcb.position.set(g.cx, y0 + pcbH / 2, g.cz);
    add(pcb);
    var top = y0 + pcbH;
    var sh = boxMesh(g.w * 0.55, 2.6, g.d * 0.58, matStd(COL_SILVER, { rough: 0.3, metal: 0.75 }));
    sh.position.set(g.cx + g.w * 0.06, top + 1.3, g.cz - g.d * 0.06);
    add(sh);
    // жёлтый IPX у левого края — туда уходит пигтейл
    var ipx = boxMesh(2.2, 1.4, 2.2, matStd(0xd9b13c, { rough: 0.45, metal: 0.3 }));
    ipx.position.set(g.cx - g.w / 2 + 1.6, top + 0.7, g.cz);
    add(ipx, false);
    // SIM-держатель с правого торца
    var sim = boxMesh(g.w * 0.16, 1.2, g.d * 0.5, matStd(0xcfd4d9, { rough: 0.35, metal: 0.7 }));
    sim.position.set(g.cx + g.w / 2 - g.w * 0.08, top + 0.6, g.cz + g.d * 0.16);
    add(sim, false);
  }

  /* --- LM2596: синяя плата, два электролита, тороид, подстроечник --- */
  function buildBuck(comp, g, y0, add) {
    var pcbH = 1.8;
    var pcb = boxMesh(g.w, pcbH, g.d, matStd(0x1e3f8c, { rough: 0.6 }));
    pcb.position.set(g.cx, y0 + pcbH / 2, g.cz);
    add(pcb);
    var top = y0 + pcbH;
    var capH = Math.min(g.h - pcbH - 1, 9);
    [-0.34, 0.10].forEach(function (k) {
      var c = cylMesh(3.1, capH, matStd(0x191d22, { rough: 0.4 }), 18);
      c.position.set(g.cx + g.w * k, top + capH / 2, g.cz - g.d * 0.12);
      add(c);
      var cap = cylMesh(3.12, 0.4, matStd(0xb9bec7, { rough: 0.3, metal: 0.6 }), 18);
      cap.position.set(c.position.x, top + capH + 0.2, c.position.z);
      add(cap, false);
    });
    var tor = new THREE.Mesh(new THREE.TorusGeometry(3.6, 1.7, 10, 20),
      matStd(0xb0a86b, { rough: 0.7 }));
    tor.rotation.x = Math.PI / 2;
    tor.position.set(g.cx + g.w * 0.30, top + 1.9, g.cz + g.d * 0.10);
    add(tor);
    var trim = boxMesh(4.6, 4.2, 4.6, matStd(0x2a5bd7, { rough: 0.5 }));
    trim.position.set(g.cx - g.w * 0.10, top + 2.1, g.cz + g.d * 0.22);
    add(trim);
    var screw = cylMesh(1.5, 0.7, matStd(0xe8ecef, { rough: 0.35, metal: 0.4 }), 14);
    screw.position.set(trim.position.x, top + 4.55, trim.position.z);
    add(screw, false);
    var slot = boxMesh(2.2, 0.2, 0.5, matStd(0x777c82, { rough: 0.4 }));
    slot.position.set(trim.position.x, top + 4.95, trim.position.z);
    add(slot, false);
  }

  /* --- Реле HF46F: глянцевый голубой корпус с маркировкой на крышке --- */
  function buildRelay(comp, g, y0, add) {
    var body = boxMesh(g.w, g.h, g.d, matStd(0x6d9fd6, { rough: 0.22, metal: 0.05 }));
    body.position.set(g.cx, y0 + g.h / 2, g.cz);
    add(body);
    var lab = new THREE.Mesh(
      new THREE.PlaneGeometry(g.w * 0.9, g.d * 0.78),
      matStd(0xffffff, {
        rough: 0.5,
        map: textTex(['HF46F-G', '012-HS1T'], { bg: '#5d8fc6', fg: '#eaf2fa', w: 256, h: 96, fs: 32 })
      })
    );
    lab.rotation.x = -Math.PI / 2;
    lab.position.set(g.cx, y0 + g.h + 0.06, g.cz);
    add(lab, false);
  }

  /* --- BC337: корпус TO-92 (цилиндр + плоская грань) на трёх ножках --- */
  function buildTransistor(comp, g, y0, add) {
    var cy = y0 + 3.2 + 1.8;
    var bodyMat = matStd(0x22262b, { rough: 0.55 });
    var body = cylMesh(2.1, 3.6, bodyMat, 20);
    body.position.set(g.cx, cy, g.cz);
    add(body);
    var flat = boxMesh(0.8, 3.6, 3.2, bodyMat);
    flat.position.set(g.cx + 1.7, cy, g.cz);
    add(flat, false);
    var lm = matStd(COL_LEAD, { rough: 0.35, metal: 0.8 });
    (comp.pins || []).forEach(function (p) {
      if (!p.hole) return;
      add(spanCylinder(
        new THREE.Vector3(g.cx, cy - 1.8, g.cz),
        new THREE.Vector3(hx(p.hole[0]), TOP + 0.2, hz(p.hole[1])),
        0.22, lm, 6), false);
    });
  }

  /* --- 1N4007: чёрный цилиндр лёжа, белое кольцо катода у вывода K --- */
  function buildDiode(comp, g, y0, add) {
    var pa = null, pk = null;
    (comp.pins || []).forEach(function (p) {
      if (p.name === 'A') pa = p.hole;
      if (p.name === 'K') pk = p.hole;
    });
    if (!pa || !pk) { genericBox(comp, g, y0, add); return; }
    var y = y0 + 1.7;
    var A = new THREE.Vector3(hx(pa[0]), y, hz(pa[1]));
    var K = new THREE.Vector3(hx(pk[0]), y, hz(pk[1]));
    var dir = new THREE.Vector3().subVectors(K, A).normalize();
    var full = A.distanceTo(K);
    var bodyLen = Math.max(full - 4.5, 3);
    var c = A.clone().lerp(K, 0.5);
    var b0 = c.clone().addScaledVector(dir, -bodyLen / 2);
    var b1 = c.clone().addScaledVector(dir, bodyLen / 2);   // конец со стороны K
    var lm = matStd(COL_LEAD, { rough: 0.35, metal: 0.8 });
    add(spanCylinder(A, K, 0.24, lm, 6), false);
    add(spanCylinder(A, new THREE.Vector3(A.x, TOP + 0.1, A.z), 0.24, lm, 6), false);
    add(spanCylinder(K, new THREE.Vector3(K.x, TOP + 0.1, K.z), 0.24, lm, 6), false);
    add(spanCylinder(b0, b1, 1.5, matStd(0x191c20, { rough: 0.5 }), 14));
    // широкое белое кольцо катода — обучающий момент: полоска смотрит на K (+12 В)
    var rc = b1.clone().addScaledVector(dir, -1.3);
    add(spanCylinder(rc.clone().addScaledVector(dir, -0.65), rc.clone().addScaledVector(dir, 0.65),
      1.58, matStd(0xe8eaee, { rough: 0.45 }), 14), false);
  }

  /* --- Резистор: бежевый цилиндр с кольцами номинала из label --- */
  function buildResistor(comp, g, y0, add) {
    var pins = comp.pins || [];
    var h1 = pins[0] && pins[0].hole, h2 = pins[1] && pins[1].hole;
    if (!h1 || !h2) { genericBox(comp, g, y0, add); return; }
    var y = y0 + 1.5;
    var P1 = new THREE.Vector3(hx(h1[0]), y, hz(h1[1]));
    var P2 = new THREE.Vector3(hx(h2[0]), y, hz(h2[1]));
    var dir = new THREE.Vector3().subVectors(P2, P1).normalize();
    var full = P1.distanceTo(P2);
    var bodyLen = Math.max(full * 0.55, 3.4);
    var c = P1.clone().lerp(P2, 0.5);
    var b0 = c.clone().addScaledVector(dir, -bodyLen / 2);
    var b1 = c.clone().addScaledVector(dir, bodyLen / 2);
    var lm = matStd(COL_LEAD, { rough: 0.35, metal: 0.8 });
    add(spanCylinder(P1, P2, 0.22, lm, 6), false);
    add(spanCylinder(P1, new THREE.Vector3(P1.x, TOP + 0.1, P1.z), 0.22, lm, 6), false);
    add(spanCylinder(P2, new THREE.Vector3(P2.x, TOP + 0.1, P2.z), 0.22, lm, 6), false);
    add(spanCylinder(b0, b1, 1.15, matStd(0xd9c39a, { rough: 0.6 }), 12));
    var ts = [0.16, 0.34, 0.52, 0.82];
    resistorBands(comp.label).forEach(function (bandCol, i) {
      var bc = b0.clone().lerp(b1, ts[i]);
      add(spanCylinder(bc.clone().addScaledVector(dir, -0.34), bc.clone().addScaledVector(dir, 0.34),
        1.24, matStd(bandCol, { rough: 0.55 }), 12), false);
    });
  }

  /* --- Конденсаторы: электролит с полосой минуса / жёлтый диск керамики --- */
  function buildCapacitor(comp, g, y0, add) {
    if (g.h >= 8) {
      var r = Math.max(Math.min(g.w, g.d) / 2 - 0.4, 1.6);
      var h = g.h - 1;
      var side = new THREE.Mesh(
        new THREE.CylinderGeometry(r, r, h, 24, 1, true),
        matStd(0xffffff, { rough: 0.5, map: capStripeTex() })
      );
      // полоса минуса (шов текстуры, локальный +Z) — в сторону вывода «−»
      var minus = pinHolePos(comp, '-');
      if (minus) side.rotation.y = Math.atan2(minus.x - g.cx, minus.z - g.cz);
      side.position.set(g.cx, y0 + h / 2, g.cz);
      add(side);
      var lid = new THREE.Mesh(new THREE.CircleGeometry(r, 24),
        matStd(0xffffff, { rough: 0.4, map: capTopTex() }));
      lid.rotation.x = -Math.PI / 2;
      lid.position.set(g.cx, y0 + h + 0.02, g.cz);
      add(lid, false);
      var base = cylMesh(r - 0.1, 0.8, matStd(0x101317, { rough: 0.6 }), 24);
      base.position.set(g.cx, y0 + 0.4, g.cz);
      add(base, false);
    } else {
      var pins = comp.pins || [];
      var h1 = pins[0] && pins[0].hole, h2 = pins[1] && pins[1].hole;
      if (!h1 || !h2) { genericBox(comp, g, y0, add); return; }
      var y = y0 + 2.6;
      var P1 = new THREE.Vector3(hx(h1[0]), TOP, hz(h1[1]));
      var P2 = new THREE.Vector3(hx(h2[0]), TOP, hz(h2[1]));
      var dir = new THREE.Vector3().subVectors(P2, P1).normalize();
      var perp = new THREE.Vector3(-dir.z, 0, dir.x);
      var c = new THREE.Vector3(g.cx, y, g.cz);
      add(spanCylinder(c.clone().addScaledVector(perp, -0.55), c.clone().addScaledVector(perp, 0.55),
        2.2, matStd(0xd9a83c, { rough: 0.6 }), 18));
      var lm = matStd(COL_LEAD, { rough: 0.35, metal: 0.8 });
      [P1, P2].forEach(function (P) {
        add(spanCylinder(P, new THREE.Vector3(P.x, y - 0.3, P.z), 0.2, lm, 6), false);
      });
    }
  }

  /* --- Клеммник KF301: зелёный блок, винты сверху, отверстия с торца --- */
  function buildTerminal(comp, g, y0, add) {
    var bh = Math.max(g.h * 0.8, 6);
    var body = boxMesh(g.w, bh, g.d, matStd(0x2e8552, { rough: 0.55 }));
    body.position.set(g.cx, y0 + bh / 2, g.cz);
    add(body);
    (comp.pins || []).forEach(function (p) {
      if (!p.hole) return;
      var sx = hx(p.hole[0]);
      var screw = cylMesh(1.7, 0.9, matStd(0x3c4046, { rough: 0.35, metal: 0.6 }), 14);
      screw.position.set(sx, y0 + bh + 0.45, g.cz - g.d * 0.15);
      add(screw, false);
      var slot = boxMesh(2.5, 0.18, 0.5, matStd(0x14171c, { rough: 0.4 }));
      slot.position.set(sx, y0 + bh + 0.95, g.cz - g.d * 0.15);
      add(slot, false);
      // отверстие под провод — с торца, куда смотрят винты (нижний край платы)
      var hole = cylMesh(1.5, 1.6, matStd(0x0d0f12, { rough: 0.8 }), 12);
      hole.rotation.x = Math.PI / 2;
      hole.position.set(sx, y0 + bh * 0.35, g.cz + g.d / 2);
      add(hole, false);
    });
  }

  /* --- Держатель предохранителя 5×20 на плату, закрытый ----------------
     Тёмный пластиковый корпус по body, сверху — круглая крышка-цилиндр
     с накаткой и прорезью под монету/отвёртку: её откручивают, чтобы
     сменить вставку. Верхняя часть корпуса дымчатая — сквозь неё виден
     намёк на стеклянную вставку между контактами. Выводы IN/OUT —
     золочёные пятачки, как у сетевого модуля. --- */
  function buildFuse(comp, g, y0, add) {
    var pins = (comp.pins || []).filter(function (p) { return p.hole; });
    var lift = 1.2;                                     // корпус стоит на выводах
    var y1 = y0 + lift;
    var H = Math.max(g.h - lift, 6);
    var baseH = Math.min(4.5, H * 0.3);                 // глухой цоколь
    var capH = Math.max(Math.min(4.0, H * 0.22), 2.4);  // крышка
    var shellH = Math.max(H - baseH - capH, 4);         // дымчатая обечайка

    var base = boxMesh(g.w, baseH, g.d, matStd(0x15181d, { rough: 0.75 }));
    base.position.set(g.cx, y1 + baseH / 2, g.cz);
    add(base);

    // стеклянная вставка лежит между выводами — это и есть смысл держателя
    if (pins.length >= 2) {
      var A = new THREE.Vector3(hx(pins[0].hole[0]), 0, hz(pins[0].hole[1]));
      var Bp = new THREE.Vector3(hx(pins[1].hole[0]), 0, hz(pins[1].hole[1]));
      var gy = y1 + baseH + shellH * 0.42;
      var dir = new THREE.Vector3().subVectors(Bp, A).normalize();
      var c = A.clone().lerp(Bp, 0.5).setY(gy);
      var half = Math.max(A.distanceTo(Bp) / 2 - 1.4, 4);   // вставка 5×20 мм
      var e1 = c.clone().addScaledVector(dir, -half);
      var e2 = c.clone().addScaledVector(dir, half);
      add(spanCylinder(e1, e2, 2.5,
        matStd(0xf2e3b4, { rough: 0.18, metal: 0.25, opacity: 0.78 }), 16), false);
      add(spanCylinder(e1, e2, 0.28, matStd(0x8d5a2a, { rough: 0.5, metal: 0.6 }), 6), false);
      [[e1, dir.clone().negate()], [e2, dir]].forEach(function (pair) {
        var k = pair[0].clone().addScaledVector(pair[1], -1.4);
        add(spanCylinder(k, k.clone().addScaledVector(pair[1], 2.8), 2.65,
          matStd(0xb9bec7, { rough: 0.3, metal: 0.85 }), 16), false);
      });
    }

    // обечайка корпуса: дымчатый пластик, сквозь него вставку видно
    var shell = boxMesh(g.w * 0.88, shellH, g.d * 0.9,
      matStd(0x1b1f25, { rough: 0.62, opacity: 0.68 }));
    shell.position.set(g.cx, y1 + baseH + shellH / 2, g.cz);
    add(shell);
    var edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(shell.geometry),
      new THREE.LineBasicMaterial({ color: 0x0b0e13, transparent: true, opacity: 0.6 })
    );
    edges.position.copy(shell.position);
    add(edges, false);

    // крышка: накатка (гранёный цилиндр) + гладкий верх + прорезь
    var capR = Math.max(Math.min(g.w, g.d) * 0.30, 3.0);
    var capY = y1 + baseH + shellH;
    var knurl = cylMesh(capR, capH * 0.72, matStd(0x2c3138, { rough: 0.45 }), 20);
    knurl.position.set(g.cx, capY + capH * 0.36, g.cz);
    add(knurl);
    var top = cylMesh(capR * 0.94, capH * 0.3, matStd(0x3a4048, { rough: 0.4 }), 24);
    top.position.set(g.cx, capY + capH * 0.85, g.cz);
    add(top, false);
    var slot = boxMesh(capR * 1.5, 0.5, 1.1, matStd(0x0d0f12, { rough: 0.8 }));
    slot.position.set(g.cx, capY + capH * 0.98, g.cz);
    add(slot, false);

    // золочёные пятачки выводов — к ним приходят навесные жилы фазы
    pins.forEach(function (p) {
      var pad = cylMesh(1.3, 0.35, matStd(COL_GOLD, { rough: 0.35, metal: 0.8 }), 14);
      pad.position.set(hx(p.hole[0]), y0 + 0.2, hz(p.hole[1]));
      add(pad, false);
    });
  }

  /* --- Светодиод 3 мм: цилиндр с куполом и ободком --------------------- */
  function buildLed(comp, g, y0, add) {
    var pins = (comp.pins || []).filter(function (p) { return p.hole; });
    // корпус стоит над анодом (первая ножка), катод подгибается к своему пятачку
    var cx = pins.length ? hx(pins[0].hole[0]) : g.cx;
    var cz = pins.length ? hz(pins[0].hole[1]) : g.cz;
    var body = cylMesh(1.5, 3.2, matStd(0xe53935, { rough: 0.3, opacity: 0.82 }), 16);
    body.position.set(cx, y0 + 3.0, cz);
    add(body);
    var dome = new THREE.Mesh(
      new THREE.SphereGeometry(1.5, 14, 10, 0, Math.PI * 2, 0, Math.PI / 2),
      matStd(0xff6f60, { rough: 0.25, opacity: 0.85 })
    );
    dome.position.set(cx, y0 + 4.6, cz);
    add(dome);
    var rim = cylMesh(1.85, 0.5, matStd(0xc62828, { rough: 0.4, opacity: 0.9 }), 16);
    rim.position.set(cx, y0 + 1.6, cz);
    add(rim, false);
    // ножка к катоду
    if (pins.length > 1) {
      var kx = hx(pins[1].hole[0]), kz = hz(pins[1].hole[1]);
      add(spanCylinder(new THREE.Vector3(cx, y0 + 1.4, cz),
        new THREE.Vector3(kx, y0 + 0.3, kz), 0.28,
        matStd(0xb9bec7, { rough: 0.3, metal: 0.85 }), 8), false);
    }
  }

  /* --- Узел пайки («звезда земли») ------------------------------------
     Не деталь, а несколько пятачков, спаянных обрезками лужёной проволоки
     по НИЖНЕЙ стороне платы: каждый луч уходит со своего пятачка. Поэтому
     рисуем сами перемычки (comp.bridges) проволокой и каплю припоя на
     каждом задетом пятачке — одной общей каплей это не показать. --- */
  function buildJunction(comp, g, y0, add) {
    var bridges = comp.bridges || [];
    var solderMat = matStd(0xb9bec7, { rough: 0.28, metal: 0.85 });

    if (!bridges.length) {
      // старые данные без перемычек — прежняя приплюснутая капля
      var r = Math.min(g.w, g.d) * 0.42;
      var blob = new THREE.Mesh(new THREE.SphereGeometry(r, 18, 12),
        matStd(0xc4c9cf, { rough: 0.18, metal: 0.9 }));
      blob.scale.y = 0.45;
      blob.position.set(g.cx, y0 + r * 0.4, g.cz);
      add(blob);
      return;
    }

    var wireMat = matStd(0xd2d7dd, { rough: 0.22, metal: 0.9 });
    var yWire = BOT - 0.6;        // проволока лежит плашмя на пятачках снизу
    var pads = {};

    // Перемычка принадлежит и узлу, и цепи: те же пути лежат в routes цепи,
    // поэтому она должна загораться и при выборе узла, и при подсветке цепи
    // (например на шаге «Лучи земли», где выбраны только цепи).
    var netId = null;
    (comp.pins || []).forEach(function (p) { if (!netId && p.net) netId = p.net; });
    function addBridge(mesh, pick) {
      var m = add(mesh, pick);
      m.userData.meta = { kind: 'bridge', compId: comp.id, netId: netId };
      return m;
    }

    bridges.forEach(function (br) {
      var p = br.path || [];
      p.forEach(function (h, i) {
        pads[h[0] + ',' + h[1]] = h;
        if (!i) return;
        addBridge(spanCylinder(
          new THREE.Vector3(hx(p[i - 1][0]), yWire, hz(p[i - 1][1])),
          new THREE.Vector3(hx(h[0]), yWire, hz(h[1])),
          0.5, wireMat, 10));
      });
    });

    Object.keys(pads).forEach(function (k) {
      var h = pads[k];
      var drop = new THREE.Mesh(new THREE.SphereGeometry(1.2, 12, 9), solderMat);
      drop.scale.y = 0.55;
      drop.position.set(hx(h[0]), BOT - 0.32, hz(h[1]));
      addBridge(drop, false);
    });

    // подписи пятачков узла: какой луч куда — показываем при наведении/выборе
    (comp.pins || []).forEach(function (p) {
      if (!p.hole) return;
      var tag = makeLabel(p.name, { size: 2.2, color: '#dfe6ef' });
      tag.position.set(hx(p.hole[0]), TOP + 5.5, hz(p.hole[1]));
      tag.visible = false;
      gLabels.add(tag);
      register(tag, { kind: 'label', compId: comp.id, minor: true });
    });
  }

  /* --- Сетевой AC-DC модуль на плате. Два непохожих корпуса, выбираем
     по разводке пинов вдоль длинной оси:
     1) пины разнесены по двум ПРОТИВОПОЛОЖНЫМ торцам (IRM-10-12, HLK-15M12
        и другие залитые модули: сеть слева, выход 12 В справа) — глухой
        залитый кирпич белого пластика с маркировкой сверху; клеммной полосы
        и винтов у него нет, пины — золочёные пятачки в плате у торцов;
     2) все клеммы у ОДНОГО торца (RS-15-12 и родня) — металлический корпус
        с винтовой клеммной полосой во всю ширину этого торца. --- */
  var PSU_PIN_LABEL = { L: 'L', N: 'N', FG: '⏚', '-V': '−V', '+V': '+V' };

  /* Маркировка на крышке — из label компонента, чтобы шелкография во вьюере
     не расходилась с данными: "IRM-10-12 (220→12 В)" → модель + расшифровка. */
  function psuMarking(comp) {
    var s = String(comp.label || '').trim();
    var m = /^(.*?)\s*\(([^)]*)\)\s*$/.exec(s);
    return m ? [m[1], m[2]] : [s];
  }

  function buildPsu(comp, g, y0, add) {
    var pins = (comp.pins || []).filter(function (p) { return p.hole; });
    var pts = pins.map(function (p) {
      return { x: hx(p.hole[0]), z: hz(p.hole[1]), name: p.name };
    });

    // К какому торцу тяготеет каждый пин вдоль длинной оси корпуса
    var alongX = g.w >= g.d;
    var nearEnd = 0, farEnd = 0;
    pts.forEach(function (p) {
      var t = alongX ? (p.x - (g.cx - g.w / 2)) / g.w : (p.z - (g.cz - g.d / 2)) / g.d;
      if (t < 0.33) nearEnd++; else if (t > 0.67) farEnd++;
    });

    if (nearEnd && farEnd) {
      // Залитый модуль: стоит на своих пинах, под торцами виден зазор с золотом
      var lift = 2.0;
      var bh = Math.max(g.h - lift, 8);
      var body = boxMesh(g.w, bh, g.d, matStd(0xe9e6dd, { rough: 0.5 }));
      body.position.set(g.cx, y0 + lift + bh / 2, g.cz);
      add(body);
      var edges = new THREE.LineSegments(
        new THREE.EdgesGeometry(body.geometry),
        new THREE.LineBasicMaterial({ color: 0x8e8a80, transparent: true, opacity: 0.55 })
      );
      edges.position.copy(body.position);
      add(edges, false);

      // canvas-маркировка сверху — как шелкография на заливке
      var lab = new THREE.Mesh(
        new THREE.PlaneGeometry(g.w * 0.6, g.d * 0.4),
        matStd(0xffffff, {
          rough: 0.55,
          map: textTex(psuMarking(comp),
            { bg: '#e9e6dd', fg: '#2c3036', w: 512, h: 160, fs: 46 })
        })
      );
      lab.rotation.x = -Math.PI / 2;
      lab.position.set(g.cx, y0 + lift + bh + 0.06, g.cz);
      add(lab, false);

      // золочёные пятачки у пинов — к ним же приходят навесные жилы 220 В
      pts.forEach(function (p) {
        var pad = cylMesh(1.15, 0.3, matStd(COL_GOLD, { rough: 0.35, metal: 0.8 }), 14);
        pad.position.set(p.x, y0 + 0.18, p.z);
        add(pad, false);
      });
      return;
    }

    // Клеммная полоса на одном торце (как у RS-15-12): металлический корпус
    var h = g.h;
    var body2 = boxMesh(g.w, h, g.d, matStd(0xaeb5bc, { rough: 0.35, metal: 0.7 }));
    body2.position.set(g.cx, y0 + h / 2, g.cz);
    add(body2);

    // маркировка на крышке, сдвинута от клеммного торца
    var lab2 = new THREE.Mesh(
      new THREE.PlaneGeometry(g.w * 0.55, g.d * 0.45),
      matStd(0xffffff, { rough: 0.5, map: textTex([psuMarking(comp)[0]], { bg: '#aeb5bc', fg: '#20242a', w: 256, h: 80, fs: 40 }) })
    );
    lab2.rotation.x = -Math.PI / 2;
    lab2.position.set(g.cx - g.w * 0.12, y0 + h + 0.06, g.cz);
    add(lab2, false);

    if (!pts.length) return;

    // Ориентация полосы: пины выстроены вдоль Z (торец «смотрит» вбок) или вдоль X
    var xs = pts.map(function (p) { return p.x; });
    var zs = pts.map(function (p) { return p.z; });
    var alongZ = (Math.max.apply(null, zs) - Math.min.apply(null, zs)) >=
                 (Math.max.apply(null, xs) - Math.min.apply(null, xs));

    var stripH = 7, stripW = 5.2;
    var plastic = matStd(0x22262b, { rough: 0.6 });
    var divider = matStd(0x3a3f46, { rough: 0.55 });
    var screwMat = matStd(0xc9ced4, { rough: 0.3, metal: 0.7 });
    var slotMat = matStd(0x14171c, { rough: 0.4 });

    // полоса во всю ширину торца
    var sx, sz, sw, sd;
    if (alongZ) {
      sx = xs.reduce(function (a, b) { return a + b; }) / xs.length;
      sz = g.cz; sw = stripW; sd = g.d - 1;
    } else {
      sx = g.cx;
      sz = zs.reduce(function (a, b) { return a + b; }) / zs.length;
      sw = g.w - 1; sd = stripW;
    }
    var strip = boxMesh(sw, stripH, sd, plastic);
    strip.position.set(sx, y0 + stripH / 2, sz);
    add(strip);

    // перегородки между секциями — на серединах между соседними пинами
    var ords = pts.map(function (p) { return alongZ ? p.z : p.x; }).sort(function (a, b) { return a - b; });
    for (var di = 1; di < ords.length; di++) {
      var mid = (ords[di - 1] + ords[di]) / 2;
      var wall = alongZ
        ? boxMesh(sw + 0.4, stripH * 0.92, 0.6, divider)
        : boxMesh(0.6, stripH * 0.92, sd + 0.4, divider);
      wall.position.set(alongZ ? sx : mid, y0 + stripH * 0.46, alongZ ? mid : sz);
      add(wall, false);
    }

    // винт с прорезью в каждой секции + мелкая подпись клеммы над ней
    pts.forEach(function (P) {
      var sc = cylMesh(1.35, 0.9, screwMat, 12);
      sc.position.set(P.x, y0 + stripH + 0.45, P.z);
      add(sc, false);
      var slot = alongZ ? boxMesh(0.45, 0.16, 2.0, slotMat) : boxMesh(2.0, 0.16, 0.45, slotMat);
      slot.position.set(P.x, y0 + stripH + 0.95, P.z);
      add(slot, false);
      var tag = makeLabel(PSU_PIN_LABEL[P.name] || P.name, {
        size: 2.1, color: '#e8ecf2', bg: 'rgba(20,23,28,0.85)', border: 'rgba(201,206,212,0.5)'
      });
      tag.position.set(P.x, y0 + stripH + 3.1, P.z);
      gLabels.add(tag);
      register(tag, { kind: 'label', compId: comp.id });
    });
  }

  function buildComponentBody(comp, g, baseY, grp) {
    function add(mesh, pick) {
      grp.add(mesh);
      register(mesh, { kind: 'component', id: comp.id }, { pick: pick !== false });
      return mesh;
    }
    switch (comp.kind) {
      case 'module':
        if (comp.id === 'U1') return buildEsp32(comp, g, baseY, add);
        if (comp.id === 'U2') return buildCc1101(comp, g, baseY, add);
        if (comp.id === 'U3') return buildSim800(comp, g, baseY, add);
        break;
      case 'buck': return buildBuck(comp, g, baseY, add);
      case 'relay': return buildRelay(comp, g, baseY, add);
      case 'transistor': return buildTransistor(comp, g, baseY, add);
      case 'diode': return buildDiode(comp, g, baseY, add);
      case 'resistor': return buildResistor(comp, g, baseY, add);
      case 'capacitor': return buildCapacitor(comp, g, baseY, add);
      case 'terminal': return buildTerminal(comp, g, baseY, add);
      case 'junction': return buildJunction(comp, g, baseY, add);
      case 'psu': return buildPsu(comp, g, baseY, add);
      case 'fuse': return buildFuse(comp, g, baseY, add);
      case 'led': return buildLed(comp, g, baseY, add);
    }
    genericBox(comp, g, baseY, add);   // неизвестный тип — прежний параллелепипед
  }

  /* Коробка, вытянутая по горизонтальному направлению a → b: длина — по
     локальной оси Z (равна |ab|), width — поперёк, height — вверх. */
  function spanBox(a, b, width, height, mat) {
    var dir = new THREE.Vector3().subVectors(b, a);
    var len = Math.hypot(dir.x, dir.z) || 0.1;
    var mesh = new THREE.Mesh(new THREE.BoxGeometry(width, height, len), mat);
    mesh.position.copy(a).addScaledVector(dir, 0.5);
    mesh.rotation.y = Math.atan2(dir.x, dir.z);
    return mesh;
  }

  // Ставим цилиндр между двумя точками (ось геометрии — Y).
  function spanCylinder(a, b, radius, mat, segs) {
    var dir = new THREE.Vector3().subVectors(b, a);
    var len = dir.length() || 0.1;
    var mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, len, segs || 16), mat);
    mesh.position.copy(a).addScaledVector(dir, 0.5);
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
    return mesh;
  }

  /* Антенна компонента: from -> to в координатах отверстий (могут быть за краем платы).
     kind:"sma"  — жёсткий разъём + штырь вверх;
     kind:"ipx"  — гибкий пигтейл, уходящий к стенке корпуса. */
  function addAntenna(comp, grp, g, baseY) {
    var a = comp.antenna;
    if (!a || !a.from || !a.to) return;
    var y = baseY + Math.max(g.h * 0.55, 3);
    var from = new THREE.Vector3(hx(a.from[0]), y, hz(a.from[1]));
    var to = new THREE.Vector3(hx(a.to[0]), y, hz(a.to[1]));
    var gold = new THREE.MeshStandardMaterial({ color: 0xd8b64e, roughness: 0.3, metalness: 0.85 });
    var add = function (mesh) {
      grp.add(mesh);
      register(mesh, { kind: 'component', id: comp.id }, { pick: true });
    };

    if (a.kind === 'ipx') {
      // мягкий кабель: дуга с подъёмом и лёгким провисом на конце
      var mid = from.clone().lerp(to, 0.5);
      mid.y += 5;
      var curve = new THREE.CatmullRomCurve3([
        from,
        from.clone().lerp(to, 0.25).setY(y + 3.5),
        mid,
        to.clone().setY(y + 1)
      ]);
      add(new THREE.Mesh(
        new THREE.TubeGeometry(curve, 56, 0.75, 8, false),
        new THREE.MeshStandardMaterial({ color: 0x14171c, roughness: 0.7, metalness: 0.1 })
      ));
      var end = curve.getPoint(1);
      var dirN = new THREE.Vector3().subVectors(to, from).normalize();
      add(spanCylinder(end.clone().addScaledVector(dirN, -3.5), end.clone().addScaledVector(dirN, 3.5), 1.8, gold, 14));
      var pl = makeLabel('пигтейл IPX', { size: 3.3, color: '#9fd8c2' });
      pl.position.set(end.x, end.y + 7, end.z);
      gLabels.add(pl);
      register(pl, { kind: 'label', compId: comp.id });
    } else {
      // жёсткий SMA: ствол + шестигранная гайка + накатка + вертикальный штырь
      add(spanCylinder(from, to, 1.9, gold, 16));
      var dirA = new THREE.Vector3().subVectors(to, from).normalize();
      var nutC = from.clone().lerp(to, 0.30);
      add(spanCylinder(nutC.clone().addScaledVector(dirA, -1.2),
        nutC.clone().addScaledVector(dirA, 1.2), 2.6, gold, 6));
      var knC = from.clone().lerp(to, 0.72);
      add(spanCylinder(knC.clone().addScaledVector(dirA, -1.0),
        knC.clone().addScaledVector(dirA, 1.0), 2.2,
        matStd(0xc9a44a, { rough: 0.55, metal: 0.6 }), 24));
      var whip = new THREE.Mesh(
        new THREE.CylinderGeometry(0.95, 1.25, 26, 12),
        new THREE.MeshStandardMaterial({ color: 0x2b2f36, roughness: 0.55, metalness: 0.35 })
      );
      whip.position.set(to.x, to.y + 13, to.z);
      add(whip);
      var tip = makeLabel('антенна ' + String(a.kind || 'sma').toUpperCase(), { size: 3.3, color: '#d9c68a' });
      tip.position.set(to.x, to.y + 30, to.z);
      gLabels.add(tip);
      register(tip, { kind: 'label', compId: comp.id });
    }
  }

  (DATA.components || []).forEach(function (comp) {
    var g = compMetrics(comp);
    var grp = new THREE.Group();
    gComponents.add(grp);

    var baseY = TOP;

    // Гнездо PBS под модулями — тонкая тёмная подложка. У вертикального
    // модуля гребёнка угловая и по габариту другая — её рисует свой билдер.
    if (comp.socket && comp.mount !== 'vertical') {
      var sock = new THREE.Mesh(
        new THREE.BoxGeometry(g.w + 0.4, 2.6, g.d + 0.4),
        new THREE.MeshStandardMaterial({ color: 0x2a2f38, roughness: 0.85, metalness: 0.05 })
      );
      sock.position.set(g.cx, baseY + 1.3, g.cz);
      grp.add(sock);
      register(sock, { kind: 'component', id: comp.id }, { pick: true });
      baseY += 2.6;
    }

    // «узнаваемый» корпус по типу компонента (см. билдеры выше)
    buildComponentBody(comp, g, baseY, grp);

    // ножки/пины сверху + пайка снизу: вывод (или ножка гнезда) проходит
    // плату насквозь — хвостик и капля припоя на нижней стороне показывают,
    // где деталь, когда плата перевёрнута для пайки
    var tailMat = new THREE.MeshStandardMaterial({ color: 0xb9bec7, roughness: 0.3, metalness: 0.8 });
    (comp.pins || []).forEach(function (p) {
      if (!p.hole) return;
      var pin = new THREE.Mesh(
        new THREE.CylinderGeometry(0.55, 0.55, 1.8, 8),
        new THREE.MeshStandardMaterial({ color: 0xd6b45a, roughness: 0.35, metalness: 0.8 })
      );
      pin.position.set(hx(p.hole[0]), TOP + 0.9, hz(p.hole[1]));
      grp.add(pin);
      register(pin, { kind: 'pin', id: comp.id + ':' + p.name, compId: comp.id, netId: p.net, pinName: p.name },
        { pick: true });

      var tail = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 1.5, 6), tailMat);
      tail.position.set(hx(p.hole[0]), BOT - 0.75, hz(p.hole[1]));
      grp.add(tail);
      register(tail, { kind: 'pin', id: comp.id + ':' + p.name, compId: comp.id, netId: p.net, pinName: p.name },
        { pick: true });
      var sold = new THREE.Mesh(new THREE.SphereGeometry(0.85, 8, 6), tailMat);
      sold.scale.y = 0.5;
      sold.position.set(hx(p.hole[0]), BOT - 0.18, hz(p.hole[1]));
      grp.add(sold);
      register(sold, { kind: 'pin', id: comp.id + ':' + p.name, compId: comp.id, netId: p.net, pinName: p.name });
    });

    // модуль на гребёнке: пунктирный контур гнезда и подпись С НИЖНЕЙ стороны —
    // при пайке снизу видно, где стоят ESP32 / SIM800L / CC1101
    if (comp.socket) {
      var outline = new THREE.LineSegments(
        new THREE.EdgesGeometry(new THREE.PlaneGeometry(g.w + 0.4, g.d + 0.4)),
        new THREE.LineDashedMaterial({ color: rampHex(comp.ramp), dashSize: 2.2, gapSize: 1.6,
          transparent: true, opacity: 0.9 })
      );
      outline.rotation.x = Math.PI / 2;
      outline.position.set(g.cx, BOT - 0.08, g.cz);
      outline.computeLineDistances();
      grp.add(outline);
      register(outline, { kind: 'component', id: comp.id });
      var bl = makeLabel(comp.label, { size: 3.2, color: '#cfd6df' });
      bl.position.set(g.cx, BOT - 4.2, g.cz);
      gLabels.add(bl);
      register(bl, { kind: 'label', compId: comp.id });
    }

    // Подпись компонента. У мелочи (резисторы, диоды, транзисторы) подпись
    // постоянно висеть не должна — иначе центр платы превращается в кашу:
    // такие показываем только при наведении или подсветке.
    var minor = g.h < 8;
    var lab = makeLabel(comp.label || comp.id, { size: minor ? 3.2 : 4.0 });
    lab.position.set(g.cx, baseY + g.h + (minor ? 2.6 : 3.6), g.cz);
    lab.visible = !minor;
    gLabels.add(lab);
    register(lab, { kind: 'label', compId: comp.id, minor: minor });

    // Антенна/пигтейл — строго по полю antenna в board.json
    if (comp.antenna) addAntenna(comp, grp, g, baseY);
  });

  /* ------------------------------------------------------------------ */
  /*  4. Провода снизу платы                                             */
  /* ------------------------------------------------------------------ */

  function hashStr(s) {
    var h = 0;
    for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return h;
  }

  function onEdge(hole) {
    return hole[0] <= 1 || hole[0] >= COLS || hole[1] <= 1 || hole[1] >= ROWS;
  }

  /* Самый высокий корпус в габарите маршрута. По нему поднимаем навесные
     жилы 220 В: держатель предохранителя — 18 мм, сетевой модуль и реле —
     15 мм, и провод не должен проходить сквозь них. */
  function pathObstacleHeight(path) {
    var c1 = Infinity, c2 = -Infinity, r1 = Infinity, r2 = -Infinity;
    path.forEach(function (p) {
      c1 = Math.min(c1, p[0]); c2 = Math.max(c2, p[0]);
      r1 = Math.min(r1, p[1]); r2 = Math.max(r2, p[1]);
    });
    var h = 0;
    (DATA.components || []).forEach(function (c) {
      if (!c.body) return;
      var b = rectMetrics(c.body);
      if (b.c2 < c1 || b.c1 > c2 || b.r2 < r1 || b.r1 > r2) return;
      h = Math.max(h, c.height || 0);
    });
    return h;
  }

  // Точка «уходит за край платы» — продлеваем последний отрезок наружу.
  function offBoardPoint(prev, last, y) {
    var dx = last[0] - prev[0], dy = last[1] - prev[1];
    if (dx === 0 && dy === 0) dx = -1;
    var len = Math.hypot(dx, dy) || 1;
    return new THREE.Vector3(hx(last[0]) + (dx / len) * 12, y, hz(last[1]) + (dy / len) * 12);
  }

  /* --- Обход корпусов навесными жилами 220 В --------------------------
     Пятачок сетевой перемычки часто сидит ПОД корпусом (клеммник, реле,
     держатель предохранителя): вертикальный спуск протыкал бы пластик.
     Для каждого конца ищем «выход» — точку сразу за ближайшей свободной
     гранью корпуса; жила ныряет к пятачку понизу, вдоль платы, у самой
     кромки детали. Если выходы двух концов соединяются по прямой, не
     задевая корпусов, — жила идёт низом; иначе поднимается в пролёт.
     Южный торец клеммника штрафуется: там входы под внешний кабель. */
  var WRAP_MIN_H = 6;    // корпуса ниже — жиле не препятствие
  var EXIT_M = 1.3;      // отступ выхода за грань корпуса, в шагах сетки

  function tallCompAt(col, row) {
    var hit = null;
    (DATA.components || []).forEach(function (c) {
      if (!c.body || (c.height || 0) < WRAP_MIN_H) return;
      var b = c.body;
      if (col >= b[0] - 0.05 && col <= b[2] + 0.05 &&
          row >= b[1] - 0.05 && row <= b[3] + 0.05) {
        if (!hit || (c.height || 0) > (hit.height || 0)) hit = c;
      }
    });
    return hit;
  }

  function wrapSegBlocked(a, b) {
    var n = Math.max(2, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / 0.6));
    for (var i = 0; i <= n; i++) {
      var t = i / n;
      if (tallCompAt(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)) return true;
    }
    return false;
  }

  function exitCandidates(hole) {
    var comp = tallCompAt(hole[0], hole[1]);
    if (!comp) return [{ at: hole, dist: 0, pen: 0 }];
    var b = comp.body;
    var res = [];
    [
      { at: [b[0] - EXIT_M, hole[1]], dist: hole[0] - b[0], side: 'w' },
      { at: [b[2] + EXIT_M, hole[1]], dist: b[2] - hole[0], side: 'e' },
      { at: [hole[0], b[1] - EXIT_M], dist: hole[1] - b[1], side: 'n' },
      { at: [hole[0], b[3] + EXIT_M], dist: hole[1] > b[3] ? 0 : b[3] - hole[1], side: 's' }
    ].forEach(function (c) {
      if (c.at[0] < 1 || c.at[0] > COLS || c.at[1] < 1 || c.at[1] > ROWS) return;
      if (tallCompAt(c.at[0], c.at[1])) return;   // выход упирается в соседний корпус
      // южный торец клеммника занят входами внешнего кабеля — только если больше некуда
      c.pen = (comp.kind === 'terminal' && c.side === 's') ? 8 : 0;
      res.push(c);
    });
    return res.length ? res : [{ at: hole, dist: 0, pen: 0 }];
  }

  function chooseWrap(a, z) {
    var best = null;
    exitCandidates(a).forEach(function (ca) {
      exitCandidates(z).forEach(function (cz) {
        var len = Math.hypot(cz.at[0] - ca.at[0], cz.at[1] - ca.at[1]);
        var blocked = wrapSegBlocked(ca.at, cz.at);
        var cost = 3 * (ca.dist + cz.dist) + ca.pen + cz.pen + len + (blocked ? 60 : 0);
        if (!best || cost < best.cost) {
          best = { a: ca, z: cz, len: len, blocked: blocked, cost: cost };
        }
      });
    });
    return best;
  }

  /* Перемычки узлов пайки (STAR.bridges) рисует билдер компонента —
     проволокой по нижней стороне платы. В routes цепи те же пути
     продублированы (чтобы валидатор видел связность узла), и вторым,
     обычным проводом их рисовать не надо. */
  function pathKey(p) {
    return (p || []).map(function (h) { return h[0] + ',' + h[1]; }).join(';');
  }
  var BRIDGE_PATHS = {};
  (DATA.components || []).forEach(function (c) {
    (c.bridges || []).forEach(function (b) { BRIDGE_PATHS[pathKey(b.path)] = c.id; });
  });

  (DATA.nets || []).forEach(function (net) {
    var cls = netClass(net['class']);
    var mains = net['class'] === 'mains';
    var col = netHex(net);                                 // свой цвет у каждой цепи
    var jitter = (hashStr(net.id) % 3) * 0.62;             // разводим цепи по глубине
    var depth = cls.depth + jitter;
    var group = wireGroups[net['class']] || wireGroups.other;

    (net.routes || []).forEach(function (route, ri) {
      var path = route.path || [];
      if (path.length < 2) return;
      if (BRIDGE_PATHS[pathKey(path)]) return;   // это перемычка узла, её рисует сам узел

      var last = path[path.length - 1];
      var exits = net.offBoard && onEdge(last);   // маршрут уходит за край платы

      /* Сторона монтажа — строго из данных (net.side). ВСЯ разводка, включая
         сеть 220, идёт ПОД платой ("bottom"): сверху остаются только корпуса
         компонентов. Жилы 220 В отличаются лишь тем, что не выходят за пределы
         островка 220, где нет низковольтных трасс. Ветка "top" (навесной
         провод с боковыми взлётами) сохранена для будущих исключений. */
      var aerial = net.side === 'top';
      var pts = [];

      if (aerial) {
        /* Жилы 220 В идут видимым «жгутом» НАД зоной 220 — так же читаемо,
           как разводка снизу платы: у пятачка жила выныривает сбоку от
           корпуса (боковой «выход», см. chooseWrap — сквозь пластик ничего
           не проходит), взлетает вертикально, летит по воздуху выше всех
           корпусов на пути и ныряет к дальнему пятачку тоже сбоку. */
        var A = new THREE.Vector3(hx(path[0][0]), TOP + 0.4, hz(path[0][1]));
        var Z = new THREE.Vector3(hx(last[0]), TOP + 0.4, hz(last[1]));
        var wrap = (path.length === 2 && !exits) ? chooseWrap(path[0], last) : null;
        var ea = wrap ? new THREE.Vector3(hx(wrap.a.at[0]), TOP + 1.6, hz(wrap.a.at[1])) : null;
        var ez = wrap ? new THREE.Vector3(hx(wrap.z.at[0]), TOP + 1.6, hz(wrap.z.at[1])) : null;

        // высота пролёта: выше самого высокого корпуса на пути, минимум 14 мм
        var arcPts = path.slice();
        if (wrap) arcPts = arcPts.concat([wrap.a.at, wrap.z.at]);
        var yArc = TOP + Math.max(14, pathObstacleHeight(arcPts) + 4) + (hashStr(net.id + ri) % 3) * 1.5;
        pts.push(A);
        if (ea) pts.push(ea);
        // взлёт у выхода из-под корпуса (или у самого пятачка, если он открыт)
        pts.push(new THREE.Vector3(ea ? ea.x : A.x, yArc - 2.4, ea ? ea.z : A.z));
        if (path.length === 2) {
          var mx = ((ea ? ea.x : A.x) + (ez ? ez.x : Z.x)) / 2;
          var mz = ((ea ? ea.z : A.z) + (ez ? ez.z : Z.z)) / 2;
          pts.push(new THREE.Vector3(mx, yArc, mz));
        } else {
          // дуга через промежуточные точки path — на высоте пролёта
          for (var k = 1; k < path.length - 1; k++) {
            pts.push(new THREE.Vector3(hx(path[k][0]), yArc, hz(path[k][1])));
          }
        }
        if (exits) {
          // навесная жила, уходящая за край: спуска на пятачок нет, уводим по воздуху
          pts.push(offBoardPoint(path[path.length - 2], last, yArc));
        } else {
          pts.push(new THREE.Vector3(ez ? ez.x : Z.x, yArc - 2.4, ez ? ez.z : Z.z));
          if (ez) pts.push(ez);
          pts.push(Z);
        }
      } else if ((route.medium || 'wire') === 'trace') {
        /* «Дорожка»: голая лужёная проволока, прижатая к плате и пропаянная
           на КАЖДОМ пятачке по пути — прямые отрезки без провиса. */
        var yT = BOT - 0.85;
        pts.push(new THREE.Vector3(hx(path[0][0]), BOT - 0.15, hz(path[0][1])));
        for (var t = 0; t < path.length; t++) {
          pts.push(new THREE.Vector3(hx(path[t][0]), yT, hz(path[t][1])));
        }
        pts.push(new THREE.Vector3(hx(last[0]), BOT - 0.15, hz(last[1])));
      } else {
        var yRun = -depth;

        // вертикальный «хвостик» от пятачка вниз, на уровень разводки
        pts.push(new THREE.Vector3(hx(path[0][0]), BOT - 0.2, hz(path[0][1])));
        pts.push(new THREE.Vector3(hx(path[0][0]), yRun, hz(path[0][1])));

        for (var i = 1; i < path.length; i++) {
          var a = path[i - 1], b = path[i];
          // лёгкий провис в середине отрезка, чтобы пересечения читались
          pts.push(new THREE.Vector3((hx(a[0]) + hx(b[0])) / 2, yRun - 0.55, (hz(a[1]) + hz(b[1])) / 2));
          pts.push(new THREE.Vector3(hx(b[0]), yRun, hz(b[1])));
        }

        if (exits) {
          pts.push(offBoardPoint(path[path.length - 2], last, yRun));
        } else {
          pts.push(new THREE.Vector3(hx(last[0]), BOT - 0.2, hz(last[1])));
        }
      }

      var isTrace = !aerial && (route.medium || 'wire') === 'trace';
      var curve = new THREE.CatmullRomCurve3(pts, false, 'catmullrom', isTrace ? 0.03 : 0.25);
      var seg = Math.max(32, pts.length * 10);
      var mesh = new THREE.Mesh(
        new THREE.TubeGeometry(curve, seg, isTrace ? 0.5 : cls.radius, 7, false),
        isTrace
          ? new THREE.MeshStandardMaterial({ color: 0xcdd2da, roughness: 0.28, metalness: 0.85 })
          : new THREE.MeshStandardMaterial({ color: col, roughness: 0.45, metalness: 0.12 })
      );
      group.add(mesh);
      register(mesh, { kind: 'net', id: net.id, routeIndex: ri }, { pick: true });

      // капли припоя: у дорожки — на каждом пятачке по пути, у провода — на концах
      var blobY = aerial ? TOP + 0.2 : BOT - 0.35;
      var blobHoles = isTrace ? pathAllHoles(path) : [path[0], last];
      blobHoles.forEach(function (h) {
        var blob = new THREE.Mesh(
          new THREE.SphereGeometry((isTrace ? 0.5 : cls.radius) + 0.55, 10, 8),
          new THREE.MeshStandardMaterial({ color: 0xb9bec7, roughness: 0.28, metalness: 0.85 })
        );
        blob.scale.y = 0.6;
        blob.position.set(hx(h[0]), blobY, hz(h[1]));
        group.add(blob);
        register(blob, { kind: 'net', id: net.id, routeIndex: ri }, { pick: true });
      });
    });
  });

  /* Все отверстия на пути маршрута (для паек дорожки). */
  function pathAllHoles(path) {
    var out = [];
    for (var i = 1; i < path.length; i++) {
      var a = path[i - 1], b = path[i];
      var dx = b[0] - a[0], dy = b[1] - a[1];
      var g = gcd2(Math.abs(dx), Math.abs(dy)) || 1;
      for (var k = (i === 1 ? 0 : 1); k <= g; k++) {
        out.push([a[0] + dx / g * k, a[1] + dy / g * k]);
      }
    }
    return out;
  }
  function gcd2(x, y) { while (y) { var t = x % y; x = y; y = t; } return x; }

  /* --- Перекрёстки: точки, где изолированный провод проходит поверх другой
     цепи. Наложений и общих паек в раскладке нет (см. lint_routes.py) —
     остаются только такие точечные пересечения; помечаем их кольцами, чтобы
     при монтаже было видно, где провод обязан лечь ПОВЕРХ, а не рядом. --- */
  (function markCrossings() {
    var holeNets = {};   // "c,r" -> { netId: 'end'|'pass' }
    (DATA.nets || []).forEach(function (net) {
      (net.routes || []).forEach(function (route) {
        var path = route.path || [];
        var vias = {};
        path.forEach(function (p) { vias[p[0] + ',' + p[1]] = true; });
        pathAllHoles(path).forEach(function (h) {
          var key = h[0] + ',' + h[1];
          var m = holeNets[key] = holeNets[key] || {};
          var role = vias[key] ? 'end' : 'pass';
          if (m[net.id] !== 'end') m[net.id] = role;
        });
      });
    });
    var mat = new THREE.MeshStandardMaterial({ color: 0xffb300, roughness: 0.5, metalness: 0.2 });
    Object.keys(holeNets).forEach(function (key) {
      var m = holeNets[key];
      var ids = Object.keys(m);
      if (ids.length !== 2) return;
      if (m[ids[0]] === 'end' || m[ids[1]] === 'end') return;   // пайки исключены линтом
      var cr = key.split(',');
      var ring = new THREE.Mesh(new THREE.TorusGeometry(1.15, 0.16, 6, 20), mat);
      ring.rotation.x = Math.PI / 2;
      ring.position.set(hx(+cr[0]), BOT - 0.45, hz(+cr[1]));
      var n1 = byNet[ids[0]];
      var grp = wireGroups[(n1 && n1['class']) || 'other'] || wireGroups.other;
      grp.add(ring);
      register(ring, { kind: 'net', id: ids[0], routeIndex: -1 });
    });
  })();

  /* ------------------------------------------------------------------ */
  /*  4b. Корпус: внешние узлы и кабели к плате                          */
  /* ------------------------------------------------------------------ */

  var ENC_W = 7 * PITCH;      // габарит блока-«призрака» — уже шага между узлами,
  var ENC_D = 5 * PITCH;      // чтобы кабели между ними не слипались
  var ENC_H = 9;
  // Стеночные узлы (гермовводы, предохранитель) живут на высоте платы:
  // этажерки больше нет, отверстия в стенках сделаны напротив её кромки.
  var ENC_BASE = TOP;
  var ENC_CABLE = ENC_BASE + 3.4;       // высота кабеля между двумя стеночными узлами
  // Кабели от стенок к плате ныряют под неё: до дна корпуса всего 8 мм
  // (стойки из case.scad), поэтому пробег — в верхней половине зазора
  var ENC_RUN = BOT - 3;

  var ENC_LINK_COLOR = { mains: rampHex('red'), power: rampHex('amber') };
  var ENC_LINK_RADIUS = { mains: 1.25, power: 0.95 };
  var encLinkLabels = [];               // подписи кабелей — показываем при наведении/выборе

  // Предохранитель рисуем не коробкой, а стеклянной вставкой: он маленький,
  // но через него проходит фаза ДО всего остального — это должно быть видно.
  function isFuseNode(node) {
    return /предохранит|fuse/i.test((node.label || '') + ' ' + (node.id || ''));
  }

  /* Точка на конкретных пинах компонента: "L" — один пин, "NO,COM" — середина
     между несколькими, если кабель берётся с двух соседних клемм. */
  function pinPoint(id, pinSpec) {
    if (!pinSpec) return null;
    var pts = [];
    String(pinSpec).split(',').forEach(function (nm) {
      nm = nm.trim();
      var c = byComp[id];
      if (c) (c.pins || []).forEach(function (p) {
        if (p.name === nm && p.hole) pts.push({ x: hx(p.hole[0]), z: hz(p.hole[1]) });
      });
    });
    if (!pts.length) return null;
    var x = 0, z = 0;
    pts.forEach(function (p) { x += p.x; z += p.z; });
    return { x: x / pts.length, z: z / pts.length };
  }

  // Точка привязки кабеля: стеночный узел или компонент на плате. pinSpec
  // (fromPin/toPin из board.json) прицеливает кабель в конкретную клемму
  // (NO реле, L/N клеммника), а не в центр корпуса. topSide — кабель приходит
  // на клемму СВЕРХУ (все 220 В), иначе подключается снизу, как разводка.
  function encAnchor(id, pinSpec, topSide) {
    var n = byEnc[id];
    if (n && n.at) return { x: hx(n.at[0]), z: hz(n.at[1]), y: n.h ? TOP + n.h : ENC_CABLE, outside: true };
    var pp = pinPoint(id, pinSpec);
    var c = byComp[id];
    if (c && c.body) {
      var g = rectMetrics(c.body);
      return {
        x: pp ? pp.x : g.cx, z: pp ? pp.z : g.cz,
        y: topSide ? TOP + 0.3 : BOT - 0.3, outside: false
      };
    }
    return null;
  }

  encNodes.forEach(function (node, ni) {
    if (!node.at) return;
    var x = hx(node.at[0]), z = hz(node.at[1]);
    var col = rampHex(node.ramp);
    var fuse = isFuseNode(node);

    function addE(mesh, pick) {
      gEnclosure.add(mesh);
      register(mesh, { kind: 'enclosure', id: node.id }, { pick: pick !== false });
      return mesh;
    }

    if (fuse) {
      // стеклянная вставка 5x20 с металлическими колпачками (лежит вдоль цепочки)
      var glass = cylMesh(2.9, 13, matStd(0xf0e4c0, { rough: 0.2, metal: 0.35, opacity: 0.62 }), 18);
      glass.rotation.z = Math.PI / 2;
      glass.position.set(x, ENC_BASE + 3.4, z);
      addE(glass);
      [-6.4, 6.4].forEach(function (dx) {
        var cap = cylMesh(3.0, 2.4, matStd(0xb9bec7, { rough: 0.3, metal: 0.85 }), 18);
        cap.rotation.z = Math.PI / 2;
        cap.position.set(x + dx, ENC_BASE + 3.4, z);
        addE(cap);
      });
    } else if (node.id === 'E-MAINS' || node.id === 'E-DRIVE') {
      // гермоввод PG9 в южной стенке: корпус вдоль Z на высоте проёма
      // (выше компонентов, см. case.scad pen_z), гайка изнутри, кабель
      // уходит наружу и вниз
      var gy = ENC_BASE + (node.h || 3.4);
      var gl = cylMesh(2.6, 8, matStd(0x9aa0a7, { rough: 0.35, metal: 0.6 }), 16);
      gl.rotation.x = Math.PI / 2;
      gl.position.set(x, gy, z + 1);
      addE(gl);
      var nut = cylMesh(3.5, 2.4, matStd(0x84898f, { rough: 0.4, metal: 0.6 }), 6);
      nut.rotation.x = Math.PI / 2;
      nut.position.set(x, gy, z - 2.4);
      addE(nut, false);
      var cable = new THREE.Mesh(
        new THREE.TubeGeometry(new THREE.CatmullRomCurve3([
          new THREE.Vector3(x, gy, z + 3),
          new THREE.Vector3(x, gy - 2.6, z + 8),
          new THREE.Vector3(x, gy - 7, z + 12)
        ]), 24, 1.25, 8, false),
        matStd(0x1a1d21, { rough: 0.7 })
      );
      addE(cable);
      if (node.id === 'E-DRIVE') {
        // за стенкой — сам привод: основание и два столбика-«ворота»
        var drvBase = boxMesh(ENC_W * 0.8, 3, ENC_D * 0.7, matStd(col, { rough: 0.7, opacity: 0.5 }));
        drvBase.position.set(x, ENC_BASE + 1.5, z + 16);
        addE(drvBase);
        [-ENC_W * 0.2, ENC_W * 0.2].forEach(function (dx) {
          var post = boxMesh(2.4, 7, 2.4, matStd(0x9aa0a7, { rough: 0.5, metal: 0.3 }));
          post.position.set(x + dx, ENC_BASE + 6.5, z + 16);
          addE(post);
        });
      }
    } else {
      // неизвестный узел — прежний полупрозрачный блок
      var gen = boxMesh(ENC_W, ENC_H, ENC_D, matStd(col, { rough: 0.8, opacity: 0.26 }));
      gen.position.set(x, ENC_BASE + ENC_H / 2, z);
      addE(gen);
    }

    // пунктирная рамка — знак того, что узел не на плате;
    // у гермовводов она висит на высоте проёма в стенке
    var frameY = node.h ? ENC_BASE + node.h : ENC_BASE + ENC_H / 2;
    var frame = new THREE.Mesh(new THREE.BoxGeometry(ENC_W, ENC_H, ENC_D));
    var dash = new THREE.LineSegments(
      new THREE.EdgesGeometry(frame.geometry),
      new THREE.LineDashedMaterial({
        color: fuse ? 0xffd166 : col, dashSize: 2.2, gapSize: 1.6,
        transparent: true, opacity: fuse ? 1 : 0.95
      })
    );
    dash.position.set(x, frameY, z);
    dash.computeLineDistances();
    gEnclosure.add(dash);
    register(dash, { kind: 'enclosure', id: node.id });
    frame.geometry.dispose();

    // подписи ставим в шахматном порядке — иначе соседние наезжают друг на друга
    var lab = makeLabel(node.label, {
      size: 3.6,
      color: fuse ? '#ffe6a8' : '#e9edf5',
      border: 'rgba(' + [col >> 16 & 255, col >> 8 & 255, col & 255].join(',') + ',0.85)',
      bg: 'rgba(9,12,17,0.78)'
    });
    lab.position.set(x, frameY + ENC_H / 2 + 3.8 + (ni % 2) * 5.2, z);
    gEnclosure.add(lab);
    register(lab, { kind: 'enclosure', id: node.id });
  });

  /* Вход в зажим клеммника: у KF301 отверстия под провод — на южном торце,
     на высоте зажима. Внешний кабель заходит туда ГОРИЗОНТАЛЬНО, а не
     падает на пятачок сверху. */
  function terminalEntry(comp, pinName) {
    var g = compMetrics(comp);
    var bh = Math.max(g.h * 0.8, 6);
    var px = null;
    (comp.pins || []).forEach(function (p) {
      if (p.name === pinName && p.hole) px = hx(p.hole[0]);
    });
    if (px === null) px = g.cx;
    return { x: px, y: TOP + bh * 0.35, zFace: g.cz + g.d / 2 };
  }

  encLinks.forEach(function (link, idx) {
    var cls = link['class'] === 'power' ? 'power' : 'mains';
    var pts;

    /* Кабель между стеночным узлом (гермовводом) и клеммником: спуск вдоль
       стенки в зазоре за краем платы, потом горизонтально в торец зажима. */
    var termId = byComp[link.from] && byComp[link.from].kind === 'terminal' ? link.from
      : byComp[link.to] && byComp[link.to].kind === 'terminal' ? link.to : null;
    var glandId = byEnc[link.from] ? link.from : byEnc[link.to] ? link.to : null;

    if (termId && glandId) {
      var t = terminalEntry(byComp[termId], termId === link.from ? link.fromPin : link.toPin);
      var nd = byEnc[glandId];
      var nx = hx(nd.at[0]), nz = hz(nd.at[1]);
      var ny = TOP + (nd.h || 3.4);
      // жилы одного кабеля расходятся веером прямо у гермоввода
      var spread = Math.max(-2.4, Math.min(2.4, (t.x - nx) * 0.12));
      pts = [
        new THREE.Vector3(nx + spread, ny, nz),
        new THREE.Vector3(nx + (t.x - nx) * 0.3, ny - 4, nz - 2.2),
        new THREE.Vector3(t.x, t.y + (ny - t.y) * 0.35, t.zFace + 6.5),
        new THREE.Vector3(t.x, t.y, t.zFace + 4.2),
        new THREE.Vector3(t.x, t.y, t.zFace + 0.3)
      ];
      drawEncLink(link, idx, cls, pts);
      return;
    }

    // side:"top" — кабель идёт поверху и опускается на клемму сверху.
    // Так заходят все 220 В: гермовводы стоят высоко, под платой сетевых
    // проводов нет вообще (см. meta.wiring в board.json).
    var overBoard = link.side ? link.side === 'top' : link['class'] === 'mains';
    var a = encAnchor(link.from, link.fromPin, overBoard);
    var b = encAnchor(link.to, link.toPin, overBoard);
    if (!a || !b) return;   // ссылка в никуда — молча пропускаем

    if (a.outside && b.outside) {
      // кабель между двумя узлами в корпусе — идёт по воздуху на их же высоте
      pts = [
        new THREE.Vector3(a.x, a.y, a.z),
        new THREE.Vector3((a.x + b.x) / 2, a.y - 1.6, (a.z + b.z) / 2),
        new THREE.Vector3(b.x, b.y, b.z)
      ];
    } else if (overBoard) {
      // подъём от стеночного узла → пролёт над всеми корпусами → вертикальный
      // спуск на клемму. Высота пролёта выше самого высокого корпуса (25 мм).
      var yFly = TOP + 26 + (idx % 3) * 1.6;
      pts = [
        new THREE.Vector3(a.x, a.y, a.z),
        new THREE.Vector3(a.x, yFly - 4, a.z),
        new THREE.Vector3(a.x * 0.6 + b.x * 0.4, yFly, a.z * 0.6 + b.z * 0.4),
        new THREE.Vector3(b.x, yFly, b.z),
        new THREE.Vector3(b.x, yFly - 4, b.z),
        new THREE.Vector3(b.x, b.y + 2.5, b.z),
        new THREE.Vector3(b.x, b.y, b.z)
      ];
    } else {
      // Кабель от стенки к плате: вертикаль у обоих концов и горизонтальный
      // пробег под платой — там только разводка, корпуса не задеваются.
      // Развод по высоте мелкий: до дна корпуса всего 8 мм.
      var yRun = ENC_RUN - (idx % 4) * 0.8;
      pts = [
        new THREE.Vector3(a.x, a.y, a.z),
        new THREE.Vector3(a.x, yRun, a.z),
        new THREE.Vector3((a.x + b.x) / 2, yRun - 0.9, (a.z + b.z) / 2),
        new THREE.Vector3(b.x, yRun, b.z),
        new THREE.Vector3(b.x, b.y, b.z)
      ];
    }

    drawEncLink(link, idx, cls, pts);
  });

  function drawEncLink(link, idx, cls, pts) {
    var curve = new THREE.CatmullRomCurve3(pts, false, 'catmullrom', 0.2);
    var cableColor = CONDUCTOR_COLOR[link.conductor] || ENC_LINK_COLOR[cls];
    var tube = new THREE.Mesh(
      new THREE.TubeGeometry(curve, 60, ENC_LINK_RADIUS[cls], 7, false),
      new THREE.MeshStandardMaterial({ color: cableColor, roughness: 0.5, metalness: 0.1 })
    );
    gEnclosure.add(tube);
    register(tube, { kind: 'enclink', index: idx, from: link.from, to: link.to, linkClass: cls }, { pick: true });

    // «рентген»-след, как у жил 220 В на плате: кабель читается и тогда,
    // когда он скрыт клеммниками или сетевым модулем
    if (cls === 'mains') {
      var ghost = new THREE.Mesh(tube.geometry, new THREE.MeshBasicMaterial({
        color: cableColor, transparent: true, opacity: 0.24,
        depthTest: false, depthWrite: false
      }));
      ghost.renderOrder = 6;
      gEnclosure.add(ghost);
      register(ghost, { kind: 'enclink', index: idx, from: link.from, to: link.to, linkClass: cls });
    }

    if (link.label) {
      var mid = curve.getPoint(0.5);
      var ll = makeLabel(link.label, {
        size: 3.2,
        color: cls === 'power' ? '#ffd9a0' : '#ffc9cb',
        border: 'rgba(' + [ENC_LINK_COLOR[cls] >> 16 & 255, ENC_LINK_COLOR[cls] >> 8 & 255,
          ENC_LINK_COLOR[cls] & 255].join(',') + ',0.9)',
        bg: 'rgba(9,12,17,0.86)'
      });
      ll.position.set(mid.x, mid.y + 4.5, mid.z);
      ll.visible = false;
      gEnclosure.add(ll);
      encLinkLabels.push(ll);
      register(ll, { kind: 'enclink', index: idx, from: link.from, to: link.to, linkClass: cls });
    }
  }

  /* ------------------------------------------------------------------ */
  /*  4c. Контур корпуса (параметры — из hardware/enclosure/case.scad)   */
  /* ------------------------------------------------------------------ */

  // Ревизия r2, низкий профиль: внутренние габариты 160x110x46, стенка 3,
  // дно 3, крышка 3; плата отцентрована и стоит на стойках 8 мм от дна —
  // под ней проходит вся разводка (числа — из шапки case.scad).
  var STANDOFF = 8;                     // стойки основной платы
  var floorY = BOT - STANDOFF;          // внутреннее дно корпуса
  var CASE = { inX: 160, inZ2: 110, inZ: 46, wall: 3, floorT: 3, lidT: 3 };
  var CASE_HX = (CASE.inX + CASE.wall * 2) / 2;
  var CASE_HZ = (CASE.inZ2 + CASE.wall * 2) / 2;

  var gCase = new THREE.Group();
  root.add(gCase);
  (function buildCase() {
    var h = CASE.floorT + CASE.inZ + CASE.lidT;
    var cy = floorY - CASE.floorT + h / 2;
    var geo = new THREE.BoxGeometry(CASE_HX * 2, h, CASE_HZ * 2);
    var shell = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({
      color: 0x8fa3b8, roughness: 0.9, metalness: 0.05,
      transparent: true, opacity: 0.06, depthWrite: false, side: THREE.DoubleSide
    }));
    shell.position.set(0, cy, 0);
    gCase.add(shell);
    var edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(geo),
      new THREE.LineDashedMaterial({ color: 0x7c8ea3, dashSize: 3, gapSize: 2, transparent: true, opacity: 0.7 })
    );
    edges.position.copy(shell.position);
    edges.computeLineDistances();
    gCase.add(edges);
    // линия внутреннего дна — видно, на чём стоит нижний ярус
    var fg = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(-CASE.inX / 2, floorY, -CASE.inZ2 / 2),
      new THREE.Vector3(CASE.inX / 2, floorY, -CASE.inZ2 / 2),
      new THREE.Vector3(CASE.inX / 2, floorY, CASE.inZ2 / 2),
      new THREE.Vector3(-CASE.inX / 2, floorY, CASE.inZ2 / 2),
      new THREE.Vector3(-CASE.inX / 2, floorY, -CASE.inZ2 / 2)
    ]);
    var floorLine = new THREE.Line(fg,
      new THREE.LineBasicMaterial({ color: 0x7c8ea3, transparent: true, opacity: 0.45 }));
    gCase.add(floorLine);
  })();
  gCase.visible = false;   // слой «Корпус (контур)» по умолчанию выключен

  /* ------------------------------------------------------------------ */
  /*  5. Орбитальные контролы (свои, без OrbitControls из examples)      */
  /* ------------------------------------------------------------------ */

  function Orbit(cam, dom) {
    this.cam = cam;
    this.dom = dom;
    this.target = new THREE.Vector3(0, 0, 0);
    this.radius = 260;
    this.theta = -0.62;   // азимут
    this.phi = 0.92;      // полярный угол
    this.minR = 45;
    this.maxR = 1800;
    this.EPS = 0.02;
    this.anim = null;

    var self = this;
    var mode = 0, px = 0, py = 0;
    // мультитач: карта активных пальцев; два пальца = пинч-зум + панорама
    var touches = {};
    var pinchDist = 0;

    function touchList() {
      return Object.keys(touches).map(function (k) { return touches[k]; });
    }
    function midAndDist() {
      var t = touchList();
      return {
        x: (t[0].x + t[1].x) / 2, y: (t[0].y + t[1].y) / 2,
        d: Math.hypot(t[0].x - t[1].x, t[0].y - t[1].y)
      };
    }

    function down(e) {
      touches[e.pointerId] = { x: e.clientX, y: e.clientY };
      self.anim = null;
      dom.setPointerCapture(e.pointerId);
      var n = touchList().length;
      if (n === 2) {
        var m = midAndDist();
        mode = 3; px = m.x; py = m.y; pinchDist = m.d;
      } else if (n === 1) {
        if (e.button === 2 || e.button === 1 || e.shiftKey) mode = 2; else mode = 1;
        px = e.clientX; py = e.clientY;
      }
    }
    function move(e) {
      if (!mode) return;
      if (touches[e.pointerId]) {
        touches[e.pointerId].x = e.clientX;
        touches[e.pointerId].y = e.clientY;
      }
      if (mode === 3) {
        if (touchList().length < 2) return;
        var m = midAndDist();
        if (pinchDist > 0 && m.d > 0) {
          self.radius = Math.min(self.maxR, Math.max(self.minR, self.radius * pinchDist / m.d));
        }
        self.pan(m.x - px, m.y - py);
        px = m.x; py = m.y; pinchDist = m.d;
        return;
      }
      var dx = e.clientX - px, dy = e.clientY - py;
      px = e.clientX; py = e.clientY;
      if (mode === 1) {
        self.theta -= dx * 0.0055;
        self.phi = Math.min(Math.PI - self.EPS, Math.max(self.EPS, self.phi - dy * 0.0055));
      } else {
        self.pan(dx, dy);
      }
    }
    function up(e) {
      delete touches[e.pointerId];
      var t = touchList();
      if (t.length === 1) {
        // один палец остался — продолжаем вращение с него
        mode = 1; px = t[0].x; py = t[0].y;
      } else if (t.length === 0) {
        mode = 0;
      }
      try { dom.releasePointerCapture(e.pointerId); } catch (err) {}
    }
    dom.addEventListener('pointerdown', down);
    dom.addEventListener('pointermove', move);
    dom.addEventListener('pointerup', up);
    dom.addEventListener('pointercancel', up);
    dom.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    dom.addEventListener('wheel', function (e) {
      e.preventDefault();
      self.anim = null;
      var k = Math.pow(0.94, -e.deltaY * 0.012);
      self.radius = Math.min(self.maxR, Math.max(self.minR, self.radius * k));
    }, { passive: false });
  }

  Orbit.prototype.pan = function (dx, dy) {
    // сдвиг цели в плоскости экрана, масштаб зависит от дистанции
    var k = this.radius * 0.0018;
    var right = new THREE.Vector3();
    var up = new THREE.Vector3();
    this.cam.matrixWorld.extractBasis(right, up, new THREE.Vector3());
    this.target.addScaledVector(right, -dx * k);
    this.target.addScaledVector(up, dy * k);
  };

  Orbit.prototype.goTo = function (o, ms) {
    var self = this;
    // выбираем кратчайший путь по азимуту
    var t1 = o.theta !== undefined ? o.theta : this.theta;
    while (t1 - this.theta > Math.PI) t1 -= Math.PI * 2;
    while (this.theta - t1 > Math.PI) t1 += Math.PI * 2;
    this.anim = {
      t0: performance.now(),
      ms: ms || 620,
      from: { r: this.radius, th: this.theta, ph: this.phi, tg: this.target.clone() },
      to: {
        r: o.radius !== undefined ? o.radius : this.radius,
        th: t1,
        ph: o.phi !== undefined ? o.phi : this.phi,
        tg: o.target ? o.target.clone() : this.target.clone()
      }
    };
  };

  Orbit.prototype.update = function () {
    if (this.anim) {
      var k = (performance.now() - this.anim.t0) / this.anim.ms;
      if (k >= 1) { k = 1; }
      var e = k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2;   // easeInOutCubic
      var a = this.anim;
      this.radius = a.from.r + (a.to.r - a.from.r) * e;
      this.theta = a.from.th + (a.to.th - a.from.th) * e;
      this.phi = a.from.ph + (a.to.ph - a.from.ph) * e;
      this.target.lerpVectors(a.from.tg, a.to.tg, e);
      if (k === 1) this.anim = null;
    }
    var sp = Math.sin(this.phi), cp = Math.cos(this.phi);
    this.cam.position.set(
      this.target.x + this.radius * sp * Math.sin(this.theta),
      this.target.y + this.radius * cp,
      this.target.z + this.radius * sp * Math.cos(this.theta)
    );
    this.cam.lookAt(this.target);
  };

  var orbit = new Orbit(camera, renderer.domElement);

  /* --- кадрирование: плата должна целиком попадать в свободную область --- */

  // Свободный прямоугольник экрана — между боковыми панелями, шапкой и нижней строкой.
  function freeRect() {
    var W = window.innerWidth, H = window.innerHeight;
    var pad = 12;
    var r = { x1: pad, y1: pad, x2: W - pad, y2: H - pad };
    var top = document.querySelector('.topbar');
    var hud = document.querySelector('.hud');
    var pl = document.querySelector('.panel-left');
    var pr = document.querySelector('.panel-right');
    if (top) r.y1 = top.getBoundingClientRect().bottom + pad;
    if (hud) r.y2 = hud.getBoundingClientRect().top - pad;
    if (W <= 700) {
      // телефон: панели — нижние шторки, сцена живёт над ними
      var tops = [];
      if (pl) tops.push(pl.getBoundingClientRect().top);
      if (pr) tops.push(pr.getBoundingClientRect().top);
      if (tops.length) r.y2 = Math.min(r.y2, Math.min.apply(null, tops) - pad);
    } else if (pl && pr) {
      var a = pl.getBoundingClientRect(), b = pr.getBoundingClientRect();
      if (b.left - a.right > 260) { r.x1 = a.right + pad; r.x2 = b.left - pad; }
    }
    if (r.x2 - r.x1 < 120) { r.x1 = pad; r.x2 = W - pad; }
    if (r.y2 - r.y1 < 120) { r.y1 = pad; r.y2 = H - pad; }
    return r;
  }

  // Смещаем ось камеры так, чтобы центр платы попадал в центр свободной области.
  function updateViewOffset() {
    var W = window.innerWidth, H = window.innerHeight;
    var r = freeRect();
    var cx = (r.x1 + r.x2) / 2, cy = (r.y1 + r.y2) / 2;
    camera.setViewOffset(W, H, W / 2 - cx, cy - H / 2, W, H);
  }

  /* Габаритная «коробка» сцены: плата + запас на антенны и провода снизу.
     Если показаны внешние узлы корпуса — коробка расширяется до них.
     Коробка несимметричная (узлы висят слева и снизу), поэтому камера целится
     в её центр, а не в центр платы — иначе половина кадра уходит в пустоту. */
  function fitBox() {
    var b = {
      // сверху запас на самый высокий корпус (SIM800L на ребре — 25 мм)
      // и его подпись, снизу — на разводку под платой
      x1: -BOARD_W / 2 - 16, x2: BOARD_W / 2 + 16,
      y1: -14, y2: 32,
      z1: -BOARD_D / 2 - 6, z2: BOARD_D / 2 + 6
    };
    if (gEnclosure.visible) {
      encNodes.forEach(function (n) {
        if (!n.at) return;
        var x = hx(n.at[0]), z = hz(n.at[1]);
        b.x1 = Math.min(b.x1, x - ENC_W / 2 - 4);
        b.x2 = Math.max(b.x2, x + ENC_W / 2 + 4);
        b.z1 = Math.min(b.z1, z - ENC_D / 2 - 4);
        b.z2 = Math.max(b.z2, z + ENC_D / 2 + 4);
        b.y2 = Math.max(b.y2, ENC_BASE + ENC_H + 14);
      });
      b.y1 = Math.min(b.y1, ENC_BASE - 6);
    }
    if (gCase.visible) {
      b.x1 = Math.min(b.x1, -CASE_HX - 2); b.x2 = Math.max(b.x2, CASE_HX + 2);
      b.z1 = Math.min(b.z1, -CASE_HZ - 2); b.z2 = Math.max(b.z2, CASE_HZ + 2);
      b.y1 = Math.min(b.y1, floorY - CASE.floorT - 2);
      b.y2 = Math.max(b.y2, floorY + CASE.inZ + CASE.lidT + 2);
    }
    return b;
  }

  function fitCenter() {
    var b = fitBox();
    return new THREE.Vector3((b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2, (b.z1 + b.z2) / 2);
  }

  // Дистанция камеры, при которой коробка целиком влезает в свободную область.
  function fitRadius(theta, phi) {
    var W = window.innerWidth, H = window.innerHeight;
    var r = freeRect();
    var fx = (r.x2 - r.x1) / W, fy = (r.y2 - r.y1) / H;
    var tanV = Math.tan((camera.fov * Math.PI / 180) / 2);
    var halfH = tanV * fy;                       // тангенс половины угла по вертикали
    var halfW = tanV * (W / H) * fx;             // ...и по горизонтали
    var sp = Math.sin(phi), cp = Math.cos(phi);
    var eye = new THREE.Vector3(sp * Math.sin(theta), cp, sp * Math.cos(theta));
    var right = new THREE.Vector3().crossVectors(new THREE.Vector3(0, 1, 0), eye).normalize();
    if (!isFinite(right.x) || right.lengthSq() < 0.5) right.set(1, 0, 0);
    var up = new THREE.Vector3().crossVectors(eye, right).normalize();

    var box = fitBox();
    var c = fitCenter();
    var d = 40;
    var p = new THREE.Vector3();
    [box.x1, box.x2].forEach(function (x) {
      [box.z1, box.z2].forEach(function (z) {
        [box.y1, box.y2].forEach(function (y) {
          p.set(x - c.x, y - c.y, z - c.z);      // координаты относительно цели камеры
          d = Math.max(d,
            p.dot(eye) + Math.abs(p.dot(right)) / halfW,
            p.dot(eye) + Math.abs(p.dot(up)) / halfH);
        });
      });
    });
    return Math.min(orbit.maxR, d * 1.02);
  }

  // Радиус «плата ровно в кадре» для текущих углов — база, от которой считается зум пользователя.
  var fitBase = 0;

  // Пересчёт кадрирования при изменении размера окна: сохраняем относительный зум,
  // иначе после сужения окна плата вылезает за края.
  function refit() {
    var ratio = fitBase > 0 ? (orbit.anim ? orbit.anim.to.r : orbit.radius) / fitBase : 1;
    fitBase = fitRadius(orbit.theta, orbit.phi);
    var r = Math.min(orbit.maxR, Math.max(orbit.minR, fitBase * ratio));
    if (orbit.anim) orbit.anim.to.r = r; else orbit.radius = r;
  }

  var VIEWS = {
    top:    { theta: 0, phi: 0.02 },
    bottom: { theta: 0, phi: Math.PI - 0.02 },
    iso:    { theta: -0.62, phi: 0.92 }
  };
  function setView(name) {
    var v = VIEWS[name] || VIEWS.iso;
    updateViewOffset();
    fitBase = fitRadius(v.theta, v.phi);
    if (!LABEL_REF) LABEL_REF = fitBase;    // эталон масштаба подписей — первый подгон кадра
    orbit.goTo({ radius: fitBase, theta: v.theta, phi: v.phi, target: fitCenter() });
    currentView = name;
    ['top', 'bottom', 'iso'].forEach(function (n) {
      var b = document.getElementById('view-' + n);
      if (b) b.classList.toggle('on', n === name);
    });
    hint(name === 'bottom'
      ? 'Вид снизу: плата перевёрнута через нижний край — ряд 38 наверху.'
      : (name === 'top' ? 'Вид сверху: колонка 1 слева, ряд 1 наверху.' : ''));
  }
  var currentView = 'iso';

  /* ------------------------------------------------------------------ */
  /*  6. Подсветка / приглушение                                         */
  /* ------------------------------------------------------------------ */

  var focus = null;      // {components:Set, nets:Set, zones:Set, enclosure:Set} либо null
  var focusEnc = null;   // производное: какие внешние узлы и кабели считаются активными

  /* Внешние узлы подсвечиваются не только по прямому выбору: если подсвечена цепь
     класса mains — загораются и кабели 220 В в корпусе, если power — кабель 12 В. */
  function deriveEnc() {
    if (!focus) return null;
    var nodes = new Set(), links = new Set(), classes = new Set();
    focus.nets.forEach(function (id) {
      var n = byNet[id];
      if (n) classes.add(n['class']);
    });
    encLinks.forEach(function (l, i) {
      var on = focus.enclosure.has(l.from) || focus.enclosure.has(l.to) ||
               focus.components.has(l.from) || focus.components.has(l.to) ||
               classes.has(l['class']);
      if (!on) return;
      links.add(i);
      if (byEnc[l.from]) nodes.add(l.from);
      if (byEnc[l.to]) nodes.add(l.to);
    });
    focus.enclosure.forEach(function (id) { nodes.add(id); });
    return { nodes: nodes, links: links };
  }

  function isActive(meta) {
    if (!focus) return true;
    switch (meta.kind) {
      case 'component': return focus.components.has(meta.id);
      case 'pin':
      case 'bridge': return focus.components.has(meta.compId) || (!!meta.netId && focus.nets.has(meta.netId));
      case 'net': return focus.nets.has(meta.id);
      case 'zone': return focus.zones.has(meta.id);
      case 'label': return focus.components.has(meta.compId);
      case 'enclosure': return !!focusEnc && focusEnc.nodes.has(meta.id);
      case 'enclink': return !!focusEnc && focusEnc.links.has(meta.index);
      default: return true;
    }
  }

  var hoveredComp = null;   // id компонента под курсором — для показа мелких подписей
  var hoveredLink = -1;     // индекс кабеля корпуса под курсором

  function updateLabels() {
    gLabels.children.forEach(function (sp) {
      var m = sp.userData.meta;
      if (!m || !m.minor) return;
      sp.visible = !!((focus && focus.components.has(m.compId)) || hoveredComp === m.compId);
    });
    // Подписи кабелей корпуса: при наведении на кабель и когда выбран внешний узел
    // (на шагах сборки их не показываем — иначе 220 В превращается в кашу из плашек).
    var nodePicked = !!focus && focus.enclosure.size > 0;
    encLinkLabels.forEach(function (sp) {
      var i = sp.userData.meta.index;
      sp.visible = hoveredLink === i || (nodePicked && !!focusEnc && focusEnc.links.has(i));
    });
  }

  /* Смена флага transparent у уже собранного материала требует пересборки
     шейдера: без needsUpdate three.js продолжает рисовать его непрозрачным,
     и приглушение не видно вовсе. Ставим флаг только при реальном
     изменении — иначе перекомпиляция шла бы на каждый клик по всей сцене. */
  function setTransparent(m, v) {
    if (m.transparent !== v) { m.transparent = v; m.needsUpdate = true; }
  }

  function applyShade() {
    for (var i = 0; i < shaded.length; i++) {
      var obj = shaded[i];
      var m = obj.material, base = obj.userData.base;
      if (!m || !base) continue;
      if (isActive(obj.userData.meta)) {
        m.opacity = base.opacity;
        setTransparent(m, base.transparent);
        m.depthWrite = base.depthWrite;
        if (m.emissive) m.emissive.setHex(focus ? base.hi : base.emissive);
      } else {
        setTransparent(m, true);
        m.opacity = base.opacity * 0.07;
        m.depthWrite = false;
        if (m.emissive) m.emissive.setHex(0x000000);
      }
    }
    updateLabels();
  }

  function setFocus(f) {
    focus = f;
    focusEnc = deriveEnc();
    applyShade();
  }
  function mkFocus(comps, nets, zones, encl) {
    return {
      components: new Set(comps || []),
      nets: new Set(nets || []),
      zones: new Set(zones || []),
      enclosure: new Set(encl || [])
    };
  }
  function clearFocus() {
    setFocus(null);
    activeStep = -1;
    renderSteps();
    renderLists();
  }

  /* ------------------------------------------------------------------ */
  /*  7. Пикинг мышью                                                    */
  /* ------------------------------------------------------------------ */

  var raycaster = new THREE.Raycaster();
  var pointer = new THREE.Vector2();
  var hovered = null;
  var selection = null;   // {type:'component'|'net'|'zone', id}

  // Текстолит тоже участвует в пикинге: он перекрывает провода на своей обратной
  // стороне, чтобы сверху нельзя было ткнуть в невидимую цепь. Клик по нему = клик по пустоте.
  boardMesh.userData.meta = { kind: 'board' };

  function pick(ev) {
    var rect = renderer.domElement.getBoundingClientRect();
    pointer.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(pointer, camera);
    var list = pickables.filter(function (o) {
      var p = o;
      while (p) { if (!p.visible) return false; p = p.parent; }
      return true;
    });
    if (!boardMat.transparent) list.push(boardMesh);   // в режиме «просветить» плата не мешает
    var hits = raycaster.intersectObjects(list, false);
    if (!hits.length) return null;
    var obj = hits[0].object;
    return obj.userData.meta && obj.userData.meta.kind === 'board' ? null : obj;
  }

  function metaTitle(meta) {
    if (!meta) return '';
    if (meta.kind === 'component') {
      var c = byComp[meta.id];
      return c ? c.label + '  ·  ' + c.id : meta.id;
    }
    if (meta.kind === 'pin') {
      var pc = byComp[meta.compId];
      return (pc ? pc.id : meta.compId) + ' · вывод ' + meta.pinName + (meta.netId ? '  →  ' + meta.netId : '');
    }
    if (meta.kind === 'bridge') {
      var bc = byComp[meta.compId];
      return (bc ? bc.label : meta.compId) + ' · перемычка узла' +
             (meta.netId ? '  →  ' + meta.netId : '');
    }
    if (meta.kind === 'net') {
      var n = byNet[meta.id];
      return n ? n.label + '  ·  ' + netClass(n['class']).label : meta.id;
    }
    if (meta.kind === 'zone') {
      var z = byZone[meta.id];
      return z ? 'Зона: ' + z.label : meta.id;
    }
    if (meta.kind === 'enclosure') {
      var e = byEnc[meta.id];
      return (e ? e.label : meta.id) + '  ·  в корпусе, не на плате';
    }
    if (meta.kind === 'enclink') {
      var l = encLinks[meta.index];
      return l ? (l.label || (l.from + ' → ' + l.to)) : '';
    }
    return '';
  }

  var downPos = null;
  renderer.domElement.addEventListener('pointerdown', function (e) {
    downPos = { x: e.clientX, y: e.clientY, t: performance.now() };
  });
  renderer.domElement.addEventListener('pointerup', function (e) {
    if (!downPos) return;
    var moved = Math.hypot(e.clientX - downPos.x, e.clientY - downPos.y);
    var quick = performance.now() - downPos.t < 600;
    downPos = null;
    if (moved > 5 || !quick || e.button !== 0) return;   // это было вращение, а не клик

    var obj = pick(e);
    if (!obj) { selection = null; showEmptyInfo(); clearFocus(); return; }
    var meta = obj.userData.meta;
    if (meta.kind === 'component' || meta.kind === 'pin' || meta.kind === 'bridge') {
      selectComponent(meta.compId || meta.id);
    } else if (meta.kind === 'net') {
      selectNet(meta.id);
    } else if (meta.kind === 'zone') {
      selectZone(meta.id);
    } else if (meta.kind === 'enclosure') {
      selectEnclosure(meta.id);
    } else if (meta.kind === 'enclink') {
      var l = encLinks[meta.index];
      if (l) selectEnclosure(byEnc[l.from] ? l.from : l.to);
    }
  });

  renderer.domElement.addEventListener('pointermove', function (e) {
    if (downPos) return;   // во время вращения наведение не считаем
    var obj = pick(e);
    var el = document.getElementById('hud-hover');
    if (obj !== hovered) {
      hovered = obj;
      renderer.domElement.style.cursor = obj ? 'pointer' : 'default';
      if (el) el.textContent = obj ? metaTitle(obj.userData.meta) : '';
      var meta = obj ? obj.userData.meta : null;
      var comp = meta && (meta.kind === 'component' ? meta.id : meta.compId) || null;
      var link = meta && meta.kind === 'enclink' ? meta.index : -1;
      if (comp !== hoveredComp || link !== hoveredLink) {
        hoveredComp = comp;
        hoveredLink = link;
        updateLabels();
      }
    }
  });

  /* ------------------------------------------------------------------ */
  /*  8. Панели интерфейса                                               */
  /* ------------------------------------------------------------------ */

  function esc(s) {
    return String(s === undefined || s === null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
  function holeText(h) { return h ? '[' + h[0] + ', ' + h[1] + ']' : '—'; }
  function pathText(p) { return (p || []).map(function (h) { return h[0] + ',' + h[1]; }).join(' → '); }

  function hint(text) {
    var el = document.getElementById('hud-hint');
    if (el) el.textContent = text || '';
  }

  var elInfo = document.getElementById('info');
  var elSteps = document.getElementById('steps');
  var elStepDetail = document.getElementById('step-detail');
  var elNets = document.getElementById('nets');
  var elComps = document.getElementById('comps');
  var elEncl = document.getElementById('encl');
  var elLegend = document.getElementById('legend');

  function setTab(name) {
    ['steps', 'build', 'list', 'info'].forEach(function (n) {
      var b = document.querySelector('.tabs button[data-tab="' + n + '"]');
      var p = document.querySelector('.tab-pane[data-pane="' + n + '"]');
      if (b) b.classList.toggle('on', n === name);
      if (p) p.classList.toggle('on', n === name);
    });
  }

  /* ---- вкладка «Монтаж»: чек-лист укладки с памятью в localStorage ---- */

  var BUILD_KEY = 'smartgate-build-r3:';
  var holePinName = {};
  (DATA.components || []).forEach(function (c) {
    (c.pins || []).forEach(function (p) {
      if (p.hole) holePinName[p.hole[0] + ',' + p.hole[1]] = c.id + '.' + p.name;
    });
  });
  function endName(h) {
    return holePinName[h[0] + ',' + h[1]] || ('[' + h[0] + ',' + h[1] + ']');
  }
  function routeLenMM(path, medium) {
    var l = 0;
    for (var i = 1; i < path.length; i++) {
      l += Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]) * PITCH;
    }
    // запас: спуски к пятачкам, изгибы и зачистка — дорожке хватает малого
    return Math.ceil((medium === 'trace' ? l + 8 : l * 1.15 + 32) / 5) * 5;
  }
  function buildItems() {
    var items = [];
    (DATA.nets || []).forEach(function (net) {
      (net.routes || []).forEach(function (route, ri) {
        var path = route.path || [];
        if (path.length < 2) return;
        items.push({
          key: net.id + '#' + ri, net: net, ri: ri,
          trace: (route.medium || 'wire') === 'trace',
          from: endName(path[0]), to: endName(path[path.length - 1]),
          len: routeLenMM(path, route.medium || 'wire'),
          note: route.note || ''
        });
      });
    });
    return items;
  }
  function renderBuild() {
    var elList = document.getElementById('build-list');
    var elProg = document.getElementById('build-progress');
    if (!elList) return;
    var items = buildItems();
    var done = items.filter(function (it) { return localStorage.getItem(BUILD_KEY + it.key) === '1'; }).length;
    if (elProg) {
      elProg.innerHTML = '<b style="color:var(--text)">Уложено ' + done + ' из ' + items.length + '</b>' +
        ' <button class="ghost" id="build-reset" style="margin-left:8px">сбросить</button>' +
        '<div style="color:var(--text-mute);font-size:11.5px;margin-top:4px">Порядок: сначала дорожки (голая лужёнка), потом провода. Клик по строке — подсветить цепь.</div>';
    }
    var groups = [
      { title: 'Дорожки — голая лужёнка, паять первыми', filt: function (it) { return it.trace; } },
      { title: 'Провода в изоляции', filt: function (it) { return !it.trace; } }
    ];
    var html = '';
    groups.forEach(function (g2) {
      var list = items.filter(g2.filt);
      html += '<div class="section"><h4>' + g2.title + ' (' + list.length + ')</h4>';
      list.forEach(function (it) {
        var ck = localStorage.getItem(BUILD_KEY + it.key) === '1';
        var sw = it.trace ? '#cdd2da' : netCss(it.net);
        html += '<label class="check build-row" data-net="' + esc(it.net.id) + '" style="align-items:flex-start' + (ck ? ';opacity:.55' : '') + '">' +
          '<input type="checkbox" data-build="' + esc(it.key) + '"' + (ck ? ' checked' : '') + '>' +
          '<i class="sw" style="background:' + sw + ';margin-top:2px"></i>' +
          '<span style="flex:1;min-width:0"><b style="font-weight:600">' + esc(it.from) + ' → ' + esc(it.to) + '</b>' +
          ' <small>' + it.len + ' мм</small><br><small>' + esc(it.net.label) +
          (it.note ? ' · ' + esc(it.note) : '') + '</small></span></label>';
      });
      html += '</div>';
    });
    elList.innerHTML = html;
    elList.querySelectorAll('input[data-build]').forEach(function (cb) {
      cb.addEventListener('change', function (e) {
        localStorage.setItem(BUILD_KEY + cb.getAttribute('data-build'), cb.checked ? '1' : '0');
        renderBuild();
        e.stopPropagation();
      });
    });
    elList.querySelectorAll('.build-row').forEach(function (row) {
      row.addEventListener('click', function (e) {
        if (e.target.tagName === 'INPUT') return;
        selectNet(row.getAttribute('data-net'));
        setTab('build');
      });
    });
    var rst = document.getElementById('build-reset');
    if (rst) rst.addEventListener('click', function () {
      buildItems().forEach(function (it) { localStorage.removeItem(BUILD_KEY + it.key); });
      renderBuild();
    });
  }

  /* ---- информация о компоненте ---- */

  function netChip(netId) {
    if (!netId) return '<span style="color:var(--text-mute)">—</span>';
    var n = byNet[netId];
    var color = n ? netCss(n) : '#888780';
    return '<span class="netlink" data-net="' + esc(netId) + '">' +
           '<i class="sw" style="background:' + color + '"></i>' + esc(n ? n.label : netId) + '</span>';
  }

  function showComponentInfo(comp) {
    var g = compMetrics(comp);
    var meta = [];
    if (comp.socket) meta.push('гнездо: ' + comp.socket);
    if (comp.mount) meta.push('крепление: ' + (MOUNT_LABEL[comp.mount] || comp.mount));
    if (comp.fit === 'verify') meta.push('шаг выводов проверить прикладыванием');
    if (comp.clearance) {
      var cg = rectMetrics(comp.clearance);
      meta.push('свободный коридор: колонки ' + cg.c1 + '–' + cg.c2 + ', ряды ' + cg.r1 + '–' + cg.r2);
    }

    var html = '<div class="info-title"><h3>' + esc(comp.label) + '</h3>' +
               '<span class="id">' + esc(comp.id) + '</span></div>' +
               '<div><span class="tag">' + esc(KIND_LABEL[comp.kind] || comp.kind || 'элемент') + '</span> ' +
               '<span class="tag">высота ' + esc(g.h) + ' мм</span></div>' +
               '<div class="note" style="border-left-color:' + rampColor(comp.ramp) + '">' +
               '<b style="color:var(--text)">Посадочное место:</b> ' +
               'колонки ' + g.c1 + '–' + g.c2 + ', ряды ' + g.r1 + '–' + g.r2 +
               ' &nbsp;(' + g.w.toFixed(1) + ' × ' + g.d.toFixed(1) + ' мм)' +
               (meta.length ? '<br>' + esc(meta.join(' · ')) : '') + '</div>';

    if (comp.note) html += '<div class="note">' + esc(comp.note) + '</div>';
    if (comp.usb && comp.usb.note) {
      html += '<div class="note"><b style="color:var(--text)">Разъём USB-C:</b> ' +
              esc(comp.usb.note) + '</div>';
    }

    if (comp.pins && comp.pins.length) {
      html += '<div class="section"><h4>Выводы</h4><table class="pins">' +
              '<tr><th>Имя</th><th>Отверстие</th><th>Цепь</th></tr>';
      comp.pins.forEach(function (p) {
        html += '<tr><td><b>' + esc(p.name) + '</b>' +
                // role — назначение пятачка в узле: по одному проводу на отверстие
                (p.role ? '<div class="rn" style="color:var(--text-mute);font-size:11px;' +
                  'line-height:1.4;margin-top:3px">' + esc(p.role) + '</div>' : '') + '</td>' +
                '<td class="hole">' + holeText(p.hole) + '</td>' +
                '<td>' + netChip(p.net) +
                (p.internal ? '<div class="rn" style="color:var(--text-mute);font-size:11px;line-height:1.4;margin-top:3px">' +
                  esc(p.internal) + '</div>' : '') +
                '</td></tr>';
      });
      html += '</table>';
      if (comp.pins.some(function (p) { return !p.net; })) {
        html += '<div class="empty" style="font-size:11.5px">Выводы без цепи не подключаются — оставить свободными.</div>';
      }
      html += '</div>';
    }

    // перемычки узла пайки: пятачки, спаянные проволокой
    if (comp.bridges && comp.bridges.length) {
      html += '<div class="section"><h4>Перемычки узла (' + comp.bridges.length + ')</h4><ul class="routes">';
      comp.bridges.forEach(function (b) {
        html += '<li><span class="path">' + esc(pathText(b.path)) + '</span>' +
                (b.note ? '<span class="rn">' + esc(b.note) + '</span>' : '') + '</li>';
      });
      html += '</ul></div>';
    }
    elInfo.innerHTML = html;
  }

  function showNetInfo(net) {
    var cls = netClass(net['class']);
    var html = '<div class="info-title"><h3>' + esc(net.label) + '</h3>' +
               '<span class="id">' + esc(net.id) + '</span></div>' +
               '<div><span class="tag" style="border-color:' + netCss(net) + '">' +
               esc(cls.label) + '</span>' +
               (net.conductor ? ' <span class="tag" style="border-color:' + netCss(net) + '">жила ' +
                 esc({ L: 'L — фаза, коричневая', L2: 'L2 — фаза «закрыть», чёрная', N: 'N — ноль, синяя', PE: 'PE — земля, жёлто-зелёная' }[net.conductor] || net.conductor) + '</span>' : '') +
               (net.side ? ' <span class="tag" data-side="' + esc(net.side) + '">' +
                 (net.side === 'top' ? 'поверх платы, навесом' : 'под платой') + '</span>' : '') +
               (net.wire ? ' <span class="tag">' + esc(net.wire) + '</span>' : '') +
               (net.offBoard ? ' <span class="tag" style="color:#ffb3b5;border-color:var(--danger)">уходит с платы</span>' : '') +
               '</div>';

    var routes = net.routes || [];
    html += '<div class="section"><h4>Маршруты (' + routes.length + ')</h4><ul class="routes">';
    routes.forEach(function (r) {
      var med = (r.medium || 'wire') === 'trace'
        ? '<span class="tag" style="border-color:#9aa0a7;color:#cdd2da">дорожка — голая лужёнка, пайка на каждом пятачке</span> '
        : '<span class="tag">провод в изоляции</span> ';
      html += '<li>' + med + '<span class="path">' + esc(pathText(r.path)) + '</span>' +
              (r.note ? '<span class="rn">' + esc(r.note) + '</span>' : '') + '</li>';
    });
    html += '</ul></div>';

    var conn = [];
    (DATA.components || []).forEach(function (c) {
      (c.pins || []).forEach(function (p) {
        if (p.net === net.id) conn.push({ c: c, p: p });
      });
    });
    if (conn.length) {
      html += '<div class="section"><h4>Подключено</h4><table class="pins">' +
              '<tr><th>Элемент</th><th>Вывод</th><th>Отверстие</th></tr>';
      conn.forEach(function (x) {
        html += '<tr><td><span class="netlink" data-comp="' + esc(x.c.id) + '">' +
                esc(x.c.label) + '</span></td>' +
                '<td><b>' + esc(x.p.name) + '</b></td>' +
                '<td class="hole">' + holeText(x.p.hole) + '</td></tr>';
      });
      html += '</table></div>';
    }
    elInfo.innerHTML = html;
  }

  function showZoneInfo(zone) {
    var g = rectMetrics(zone.rect);
    var keepout = zone.kind === 'keepout';
    var clearance = zone.kind === 'clearance';
    var html = '<div class="info-title"><h3>' + esc(zone.label) + '</h3>' +
               '<span class="id">' + esc(zone.id) + '</span></div>' +
               '<div><span class="tag" style="border-color:' + rampColor(zone.ramp) + '">Зона платы</span>' +
               (keepout ? ' <span class="tag" style="color:#ffb3b5;border-color:var(--danger)">keep-out</span>' : '') +
               (clearance ? ' <span class="tag">коридор · ничего выше ' + (zone.height || 5) + ' мм</span>' : '') +
               '</div>' +
               '<div class="note' + (keepout ? ' warn' : '') + '">' +
               '<b style="color:var(--text)">Границы:</b> колонки ' + g.c1 + '–' + g.c2 +
               ', ряды ' + g.r1 + '–' + g.r2 + '</div>';
    if (zone.note) html += '<div class="note' + (keepout ? ' warn' : '') + '">' + esc(zone.note) + '</div>';

    var inside = (DATA.components || []).filter(function (c) {
      if (!c.body) return false;
      var b = rectMetrics(c.body);
      return b.c1 >= g.c1 && b.c2 <= g.c2 && b.r1 >= g.r1 && b.r2 <= g.r2;
    });
    if (inside.length) {
      html += '<div class="section"><h4>Внутри зоны</h4>';
      inside.forEach(function (c) {
        html += '<div class="row" data-comp="' + esc(c.id) + '">' +
                '<i class="sw" style="background:' + rampColor(c.ramp) + '"></i>' +
                '<span class="nm">' + esc(c.label) + '</span><span class="id">' + esc(c.id) + '</span></div>';
      });
      html += '</div>';
    }
    elInfo.innerHTML = html;
  }

  function showEnclosureInfo(node) {
    var links = encLinks.filter(function (l) { return l.from === node.id || l.to === node.id; });
    var nameOf = function (id) {
      if (byEnc[id]) return byEnc[id].label;
      if (byComp[id]) return byComp[id].label + ' (на плате)';
      return id;
    };
    var html = '<div class="info-title"><h3>' + esc(node.label) + '</h3>' +
               '<span class="id">' + esc(node.id) + '</span></div>' +
               '<div><span class="tag" style="border-color:' + rampColor(node.ramp) + '">В корпусе, не на плате</span>' +
               (node.side ? ' <span class="tag">со стороны: ' + esc(node.side === 'left' ? 'слева' : 'снизу') + '</span>' : '') +
               '</div>';
    if (node.detail) html += '<div class="note">' + esc(node.detail) + '</div>';

    if (links.length) {
      html += '<div class="section"><h4>Связи</h4><table class="pins">' +
              '<tr><th>Куда</th><th>Что за провод</th></tr>';
      links.forEach(function (l) {
        var other = l.from === node.id ? l.to : l.from;
        var attr = byEnc[other] ? 'data-encl="' + esc(other) + '"' : 'data-comp="' + esc(other) + '"';
        html += '<tr><td><span class="netlink" ' + attr + '>' +
                '<i class="sw" style="background:' + (l['class'] === 'power' ? RAMP.amber : RAMP.red) + '"></i>' +
                esc(nameOf(other)) + '</span></td>' +
                '<td style="color:var(--text-dim)">' + esc(l.label || '') + '</td></tr>';
      });
      html += '</table></div>';
    }
    if (ENC.note) html += '<div class="note warn">' + esc(ENC.note) + '</div>';
    elInfo.innerHTML = html;
  }

  function showEmptyInfo() {
    elInfo.innerHTML = '<div class="empty">Кликни по модулю, проводу или зоне на плате — здесь появится описание, ' +
      'таблица выводов и маршруты.<br><br>' +
      '<span class="kbd">ЛКМ</span> вращение · <span class="kbd">колесо</span> зум · ' +
      '<span class="kbd">ПКМ / Shift</span> панорама</div>';
  }

  function selectComponent(id) {
    var c = byComp[id];
    if (!c) return;
    selection = { type: 'component', id: id };
    var nets = [];
    (c.pins || []).forEach(function (p) { if (p.net) nets.push(p.net); });
    setFocus(mkFocus([id], nets, []));
    showComponentInfo(c);
    setTab('info');
    activeStep = -1;
    renderSteps();
    renderLists();
  }

  function selectNet(id) {
    var n = byNet[id];
    if (!n) return;
    selection = { type: 'net', id: id };
    var comps = [];
    (DATA.components || []).forEach(function (c) {
      (c.pins || []).forEach(function (p) { if (p.net === id) comps.push(c.id); });
    });
    setFocus(mkFocus(comps, [id], []));
    showNetInfo(n);
    setTab('info');
    activeStep = -1;
    renderSteps();
    renderLists();
    if (n.side === 'top') {
      hint('Жила идёт навесом НАД платой: взлетает сбоку от корпуса и ныряет к пятачку тоже сбоку.');
    } else if (n['class'] === 'mains' && currentView !== 'bottom') {
      hint('Сеть 220 В разведена ПОД платой, в пределах островка 220 — смотри вид «Снизу» или включи «Просветить плату».');
    } else if (currentView === 'top') {
      hint('Разводка проходит снизу платы — переключись на вид «Снизу».');
    }
  }

  function selectEnclosure(id) {
    var n = byEnc[id];
    if (!n) return;
    selection = { type: 'enclosure', id: id };
    var comps = [];
    encLinks.forEach(function (l) {
      if (l.from === id && byComp[l.to]) comps.push(l.to);
      if (l.to === id && byComp[l.from]) comps.push(l.from);
    });
    setFocus(mkFocus(comps, [], [], [id]));
    showEnclosureInfo(n);
    setTab('info');
    activeStep = -1;
    renderSteps();
    renderLists();
  }

  function selectZone(id) {
    var z = byZone[id];
    if (!z) return;
    selection = { type: 'zone', id: id };
    var comps = (DATA.components || []).filter(function (c) {
      if (!c.body) return false;
      var g = rectMetrics(z.rect), b = rectMetrics(c.body);
      return b.c1 >= g.c1 && b.c2 <= g.c2 && b.r1 >= g.r1 && b.r2 <= g.r2;
    }).map(function (c) { return c.id; });
    setFocus(mkFocus(comps, [], [id]));
    showZoneInfo(z);
    setTab('info');
    activeStep = -1;
    renderSteps();
    renderLists();
  }

  /* ---- списки цепей и компонентов ---- */

  function renderLists() {
    function compRow(c) {
      var on = selection && selection.type === 'component' && selection.id === c.id;
      return '<div class="row' + (on ? ' on' : '') + '" data-comp="' + esc(c.id) + '">' +
             '<i class="sw" style="background:' + rampColor(c.ramp) + '"></i>' +
             '<span class="nm">' + esc(c.label) + '</span>' +
             '<span class="id">' + esc(c.id) + '</span></div>';
    }
    var html = '';
    (DATA.components || []).forEach(function (c) { html += compRow(c); });
    elComps.innerHTML = html;

    if (elEncl) {
      html = '';
      encNodes.forEach(function (n) {
        var on = selection && selection.type === 'enclosure' && selection.id === n.id;
        html += '<div class="row' + (on ? ' on' : '') + '" data-encl="' + esc(n.id) + '">' +
                '<i class="sw" style="background:' + rampColor(n.ramp) + ';opacity:0.55;' +
                'outline:1px dashed ' + rampColor(n.ramp) + ';outline-offset:1px"></i>' +
                '<span class="nm">' + esc(n.label) + '</span></div>';
      });
      elEncl.innerHTML = html || '<div class="empty">Нет внешних узлов.</div>';
    }

    html = '';
    (DATA.nets || []).forEach(function (n) {
      var on = selection && selection.type === 'net' && selection.id === n.id;
      html += '<div class="row' + (on ? ' on' : '') + '" data-net="' + esc(n.id) + '">' +
              '<i class="sw" style="background:' + netCss(n) + '"></i>' +
              '<span class="nm">' + esc(n.label) + '</span>' +
              '<span class="id">' + esc(netClass(n['class']).label) + '</span></div>';
    });
    elNets.innerHTML = html;
  }

  /* ---- шаги сборки ---- */

  var activeStep = -1;
  var STEPS = DATA.steps || [];

  function renderSteps() {
    var html = '';
    STEPS.forEach(function (s, i) {
      html += '<div class="step' + (i === activeStep ? ' on' : '') + '" data-step="' + i + '">' +
              '<i class="n">' + esc(s.n !== undefined ? s.n : i + 1) + '</i>' +
              '<span class="t">' + esc(s.title) + '</span></div>';
    });
    elSteps.innerHTML = html;

    if (activeStep >= 0 && STEPS[activeStep]) {
      var s = STEPS[activeStep];
      var h = s.highlight || {};
      var bits = [];
      if (h.components && h.components.length) bits.push('элементы: ' + h.components.join(', '));
      if (h.nets && h.nets.length) bits.push('цепи: ' + h.nets.join(', '));
      if (h.zones && h.zones.length) bits.push('зоны: ' + h.zones.join(', '));
      elStepDetail.innerHTML = '<b>' + esc((s.n !== undefined ? s.n + '. ' : '') + s.title) + '</b>' +
        esc(s.detail || '') +
        (bits.length ? '<div style="margin-top:8px;font-family:var(--mono);font-size:11px;color:var(--text-mute)">' +
          esc(bits.join(' · ')) + '</div>' : '');
      elStepDetail.style.display = '';
    } else {
      elStepDetail.style.display = 'none';
    }
  }

  function gotoStep(i) {
    if (!STEPS.length) return;
    activeStep = Math.min(STEPS.length - 1, Math.max(0, i));
    var s = STEPS[activeStep];
    var h = s.highlight || {};
    selection = null;
    setFocus(mkFocus(h.components, h.nets, h.zones));
    renderSteps();
    renderLists();
    setTab('steps');
    var el = elSteps.querySelector('.step.on');
    if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' });
    // если шаг про разводку — подсказать, где её смотреть: вся разводка,
    // включая сеть 220, идёт по нижней стороне платы
    var underNets = (h.nets || []).some(function (id) {
      var n = byNet[id];
      return n && n.side !== 'top';
    });
    if ((h.nets && h.nets.length) && !(h.components && h.components.length) && currentView !== 'bottom') {
      hint(underNets
        ? 'Шаг про разводку — включи вид «Снизу», провода идут по нижней стороне.'
        : 'Навесные жилы идут над платой — смотри сверху или в изометрии.');
    } else {
      hint('');
    }
  }

  /* ---- легенда ---- */

  function renderLegend() {
    var html = '<h4>Зоны платы</h4>';
    (DATA.zones || []).forEach(function (z) {
      html += '<div class="legend-item" data-zone="' + esc(z.id) + '" style="cursor:pointer">' +
              '<i class="sw" style="background:' + rampColor(z.ramp) + '"></i>' +
              '<b>' + esc(z.label) + '</b></div>';
    });
    html += '<h4 style="margin-top:14px">Цепи</h4>';
    var seen = {};
    (DATA.nets || []).forEach(function (n) {
      var c = n['class'] || 'other';
      if (seen[c]) return;
      seen[c] = 1;
      var cls = netClass(c);
      html += '<div class="legend-item">' +
              '<i class="ln" style="width:18px;height:' + Math.max(2, cls.radius * 3) + 'px;background:' +
              rampColor(n.ramp) + '"></i><b>' + esc(cls.label) + '</b>' +
              '<span style="margin-left:auto;font-family:var(--mono);font-size:11px">' +
              cls.radius.toFixed(2) + ' мм</span></div>';
    });
    html += '<div class="legend-item" style="margin-top:6px;color:var(--text-mute)">' +
            'Цвет провода берётся из поля <code>ramp</code> цепи, толщина — из класса, ' +
            'сторона монтажа — из поля <code>side</code>: 220 В идут поверх платы, ' +
            'низковольтные — под ней.</div>';

    if (encNodes.length) {
      html += '<h4 style="margin-top:14px">Вне платы</h4>' +
              '<div class="legend-item"><i class="sw" style="background:transparent;' +
              'border:1px dashed var(--text-dim)"></i><b>Узлы в корпусе</b></div>' +
              '<div class="legend-item"><i class="ln" style="width:18px;height:4px;background:' + RAMP.red +
              '"></i><b>220 В в корпусе</b></div>';
      // строка про 12 В имеет смысл только пока существует внешний БП
      if (encLinks.some(function (l) { return l['class'] === 'power'; })) {
        html += '<div class="legend-item"><i class="ln" style="width:18px;height:3px;background:' + RAMP.amber +
                '"></i><b>12 В на плату</b></div>';
      }
    }
    elLegend.innerHTML = html;
  }

  /* ------------------------------------------------------------------ */
  /*  9. Слои                                                            */
  /* ------------------------------------------------------------------ */

  var LAYER_TARGETS = {
    power: function () { return wireGroups.power; },
    gnd: function () { return wireGroups.gnd; },
    signal: function () { return wireGroups.signal; },
    mains: function () { return wireGroups.mains; },
    components: function () { return gComponents; },
    zones: function () { return gZones; },
    labels: function () { return gLabels; },
    enclosure: function () { return gEnclosure; },
    'case': function () { return gCase; }
  };

  function wireLayers() {
    Array.prototype.forEach.call(document.querySelectorAll('input[data-layer]'), function (inp) {
      var name = inp.getAttribute('data-layer');
      // Цепи неизвестного класса (если такие появятся в board.json) живут
      // в группе other и всегда видимы — лучше показать лишнее, чем потерять.
      var apply = function () {
        var t = LAYER_TARGETS[name];
        if (t) t().visible = inp.checked;
      };
      inp.addEventListener('change', apply);
      apply();
    });

    var xray = document.getElementById('board-xray');
    if (xray) {
      var applyX = function () {
        boardMat.transparent = xray.checked;
        boardMat.opacity = xray.checked ? 0.34 : 1;
        boardMat.depthWrite = !xray.checked;
        boardMat.needsUpdate = true;
        var pm = boardMesh.userData.padMat, hm = boardMesh.userData.holeMat;
        [pm, hm].forEach(function (m) {
          if (!m) return;
          m.transparent = xray.checked;
          m.opacity = xray.checked ? 0.45 : 1;
          m.needsUpdate = true;
        });
      };
      xray.addEventListener('change', applyX);
      applyX();
    }
  }

  /* ------------------------------------------------------------------ */
  /*  10. Тема                                                           */
  /* ------------------------------------------------------------------ */

  function applyTheme(name) {
    theme = name;
    document.documentElement.setAttribute('data-theme', name);
    var t = THEMES[name];
    renderer.setClearColor(t.bg, 1);
    scene.background = new THREE.Color(t.bg);
    boardMat.color.setHex(t.board);
    boardEdges.material.color.setHex(t.boardEdge);
    hemi.color.setHex(t.hemiSky);
    hemi.groundColor.setHex(t.hemiGround);
    ambient.intensity = t.ambient;
    var pm = boardMesh.userData.padMat, hm = boardMesh.userData.holeMat;
    if (pm) pm.color.setHex(t.pad);
    if (hm) hm.color.setHex(t.hole);
    var btn = document.getElementById('theme-toggle');
    if (btn) btn.textContent = name === 'dark' ? '☾ Тёмная' : '☀ Светлая';
  }

  /* ------------------------------------------------------------------ */
  /*  11. Обработчики интерфейса                                         */
  /* ------------------------------------------------------------------ */

  function bindUI() {
    document.getElementById('view-top').addEventListener('click', function () { setView('top'); });
    document.getElementById('view-bottom').addEventListener('click', function () { setView('bottom'); });
    document.getElementById('view-iso').addEventListener('click', function () { setView('iso'); });
    document.getElementById('view-reset').addEventListener('click', function () {
      setView('iso');
      selection = null;
      showEmptyInfo();
      clearFocus();
      hint('');
    });
    document.getElementById('theme-toggle').addEventListener('click', function () {
      applyTheme(theme === 'dark' ? 'light' : 'dark');
    });

    Array.prototype.forEach.call(document.querySelectorAll('.tabs button'), function (b) {
      b.addEventListener('click', function () { setTab(b.getAttribute('data-tab')); });
    });
    // Телефон: обе панели стартуют свёрнутыми шторками; раскрытие одной
    // сворачивает другую, тап по шапке = тап по ▾.
    var isMobile = window.matchMedia && window.matchMedia('(max-width: 700px)').matches;
    var allPanels = document.querySelectorAll('.panel-left, .panel-right');
    if (isMobile) {
      Array.prototype.forEach.call(allPanels, function (p) { p.classList.add('collapsed'); });
      Array.prototype.forEach.call(document.querySelectorAll('.panel-head'), function (head) {
        head.addEventListener('click', function (e) {
          if (e.target.closest('.collapse')) return;   // кнопка сама разберётся
          var panel = head.closest('.panel');
          var opening = panel.classList.contains('collapsed');
          panel.classList.toggle('collapsed');
          if (opening) {
            Array.prototype.forEach.call(allPanels, function (p) {
              if (p !== panel) p.classList.add('collapsed');
            });
          }
        });
      });
    }
    Array.prototype.forEach.call(document.querySelectorAll('.panel-head .collapse'), function (b) {
      b.addEventListener('click', function () {
        var panel = b.closest('.panel');
        var opening = panel.classList.contains('collapsed');
        panel.classList.toggle('collapsed');
        if (isMobile && opening) {
          Array.prototype.forEach.call(allPanels, function (p) {
            if (p !== panel) p.classList.add('collapsed');
          });
        }
      });
    });

    document.getElementById('step-prev').addEventListener('click', function () {
      gotoStep(activeStep <= 0 ? 0 : activeStep - 1);
    });
    document.getElementById('step-next').addEventListener('click', function () {
      gotoStep(activeStep < 0 ? 0 : activeStep + 1);
    });
    document.getElementById('step-clear').addEventListener('click', function () {
      selection = null;
      showEmptyInfo();
      clearFocus();
      hint('');
    });

    // делегирование кликов по спискам и ссылкам внутри панелей
    document.getElementById('ui').addEventListener('click', function (e) {
      var t = e.target.closest('[data-step]');
      if (t) { gotoStep(parseInt(t.getAttribute('data-step'), 10)); return; }
      t = e.target.closest('[data-comp]');
      if (t) { selectComponent(t.getAttribute('data-comp')); return; }
      t = e.target.closest('[data-net]');
      if (t) { selectNet(t.getAttribute('data-net')); return; }
      t = e.target.closest('[data-zone]');
      if (t) { selectZone(t.getAttribute('data-zone')); return; }
      t = e.target.closest('[data-encl]');
      if (t) { selectEnclosure(t.getAttribute('data-encl')); return; }
    });

    window.addEventListener('keydown', function (e) {
      if (e.target && /input|textarea/i.test(e.target.tagName)) return;
      if (e.key === 'ArrowRight') gotoStep(activeStep < 0 ? 0 : activeStep + 1);
      else if (e.key === 'ArrowLeft') gotoStep(activeStep <= 0 ? 0 : activeStep - 1);
      else if (e.key === 'Escape') { selection = null; showEmptyInfo(); clearFocus(); }
      else if (e.key === '1') setView('top');
      else if (e.key === '2') setView('bottom');
      else if (e.key === '3') setView('iso');
    });
  }

  /* ------------------------------------------------------------------ */
  /*  12. Запуск                                                         */
  /* ------------------------------------------------------------------ */

  function resize() {
    var w = window.innerWidth, h = window.innerHeight;
    camera.aspect = w / h;
    renderer.setSize(w, h, false);
    updateViewOffset();          // внутри вызывает updateProjectionMatrix
    refit();
  }
  window.addEventListener('resize', resize);

  function tick() {
    orbit.update();
    updateLabelScale();
    renderer.render(scene, camera);
    requestAnimationFrame(tick);
  }

  // шапка
  (function fillHead() {
    var m = DATA.meta || {};
    var t = document.getElementById('brand-title');
    var s = document.getElementById('brand-sub');
    if (t) t.textContent = m.name || 'Монтажная плата';
    if (s) s.textContent = [m.revision, m.board, COLS + '×' + ROWS + ' отв., шаг ' + PITCH + ' мм']
      .filter(Boolean).join('  ·  ');
    var stat = document.getElementById('hud-stats');
    if (stat) stat.textContent = (DATA.components || []).length + ' элементов · ' +
      (DATA.nets || []).length + ' цепей · ' + (DATA.steps || []).length + ' шагов сборки';
  })();

  // Небольшой публичный API: удобно дёргать из консоли и проверять состояние сцены.
  window.BOARD_VIEWER = {
    scene: scene,
    camera: camera,
    orbit: orbit,
    setView: setView,
    setTheme: applyTheme,
    selectComponent: selectComponent,
    selectNet: selectNet,
    selectZone: selectZone,
    selectEnclosure: selectEnclosure,
    gotoStep: gotoStep,
    clearFocus: clearFocus,
    // сводка: сколько объектов сейчас приглушено — по ней видно, что подсветка сработала
    state: function () {
      var dim = 0, lit = 0;
      shaded.forEach(function (o) { (isActive(o.userData.meta) ? lit++ : dim++); });
      return {
        focus: focus ? {
          components: Array.from(focus.components),
          nets: Array.from(focus.nets),
          zones: Array.from(focus.zones),
          enclosure: Array.from(focus.enclosure)
        } : null,
        lit: lit, dimmed: dim, objects: shaded.length,
        view: currentView,
        camera: { radius: +orbit.radius.toFixed(1), theta: +orbit.theta.toFixed(2), phi: +orbit.phi.toFixed(2) }
      };
    }
  };

  applyTheme('dark');
  wireLayers();
  bindUI();
  renderLegend();
  renderLists();
  renderSteps();
  renderBuild();
  showEmptyInfo();
  setTab('steps');
  resize();          // сначала размеры и аспект, потом подгон камеры под них
  setView('iso');
  tick();
})();
