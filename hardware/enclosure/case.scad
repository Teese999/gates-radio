// ============================================================================
// Умные Ворота — корпус под 3D-печать (ревизия r3)
// ============================================================================
// Компоновка (см. hardware/board/board.json):
//   одна плата 10x15 на стойках; сетевой модуль IRM-10-12 стоит на ней.
// Влагозащита: без вентиляции, крышка с фальцем и канавкой под силиконовый
// шнур 2 мм, кабели — только через гермовводы PG9, антенны — через SMA.
//
// Проёмы в стенках подняты ВЫШЕ компонентов платы: гермовводы уходят внутрь
// на 10–15 мм и на уровне платы упирались бы в реле и сетевой модуль.
// Кабель после гермоввода спускается ВДОЛЬ стенки в зазоре за краем платы
// и заходит жилами в зажимы клеммников горизонтально, с торца (входы
// KF301 смотрят на стенку). Предохранитель стоит на плате, отверстия под
// него в стенке нет — на одну дырку меньше, влагозащита лучше.
//
// Печать: PETG или ASA (на улице PLA плывёт), 4+ периметра, заполнение 25%+,
// дном вниз без поддержек. Резьба нигде не печатается: крышка и стойки — на
// латунных вставках M3 (запрессовать паяльником).
//
// Экспорт STL: openscad -o stl/case_body.stl -D 'part="body"' case.scad
//              openscad -o stl/case_lid.stl  -D 'part="lid"'  case.scad
// ============================================================================

part = "body"; // "body" | "lid" | "preview" (обе детали рядом)

// ---------- основные параметры, мм ----------
wall      = 3;
floor_t   = 3;
lid_t     = 3;
r_out     = 4;

board_x   = 150;   // текстолит 10x15; сетка 58x38 внутри него со смещением от края
board_y   = 100;
grid_off  = [2.6, 3.0];   // от угла платы до центра пятачка [1,1] (см. board.json)
board_t   = 1.6;
standoff  = 8;     // под платой идёт вся разводка
tall_comp = 17;    // самый высокий элемент на плате (электролит 2200 мкФ)

in_x = 160;
in_y = 110;
main_off = [(in_x - board_x)/2, (in_y - board_y)/2];   // [5, 5]

// Пересчёт координат платы в координаты корпуса.
// bm/bym — миллиметры от угла текстолита, bx/by — по сетке пятачков.
function bm(x) = main_off[0] + x;
function bym(y) = main_off[1] + y;
function bx(col) = bm(grid_off[0] + (col - 1) * 2.54);
function by(row) = bym(grid_off[1] + (row - 1) * 2.54);

// Верх компонентов над внутренним дном
comp_top  = standoff + board_t + tall_comp;            // 26.6
// Проёмы в стенках — выше компонентов: половина диаметра PG9 (7.6) плюс
// зазор 5 мм до самой высокой детали и 5 мм до крышки.
pen_z     = floor_t + comp_top + 5 + 7.6;              // 42.2 от наружного дна
in_z      = 52;   // = comp_top + 5 + 7.6 (проём) + 7.6 + 5 запаса, округлено

// ---------- крепёж ----------
insert_d  = 4.0;   // отверстие под латунную вставку M3 (натяг, запрессовка паяльником)
insert_h  = 6;
m3_free   = 3.4;

// Стойки под крепёжные отверстия платы: они в зелёном поле по углам,
// в миллиметрах от угла текстолита (см. board.json -> board.mountingHoles).
main_holes = [ for (xy = [[4,4],[146,4],[4,96],[146,96]]) [bm(xy[0]), bym(xy[1])] ];

// ---------- проёмы в стенках ----------
// Зона 220 на плате — ряды 27..38, то есть у ДАЛЬНЕЙ стенки (y = in_y).
// Туда же выходят оба гермоввода. Роли назначены по клеммникам под ними:
// ACOUT (зажимы в колонках 20-24) — западнее, ACIN (27-29) — восточнее,
// поэтому привод — левый гермоввод, щиток — правый; кабели не перекрещиваются.
pg9_d       = 15.2;   // гермоввод PG9

gland_out_x = 52;     // кабель к приводу с клеммника ACOUT (зажимы ~кол. 22)
gland_in_x  = 88;     // ввод 220 из щитка -> клеммник ACIN (зажимы ~кол. 28)

// SMA-гнёзда: на уровне модулей, ряды 8 (радио, правая стенка) и 9 (GSM, левая)
sma_d       = 6.5;
sma_z       = floor_t + standoff + board_t + 8;
sma_right_y = by(8);
sma_left_y  = by(9);

// Уплотнение крышки: канавка под силиконовый шнур d2
seal_w = 2.6; seal_deep = 1.4;

$fn = 48;
eps = 0.01;

// ---------- заготовки ----------
module rbox(x, y, z, r) {
  hull() for (dx=[r, x-r], dy=[r, y-r]) translate([dx,dy,0]) cylinder(h=z, r=r);
}

module boss(h) {
  difference() {
    cylinder(h=h, d=insert_d+5);
    translate([0,0,h-insert_h]) cylinder(h=insert_h+eps, d=insert_d);
  }
}

// проём в дальней стенке (ось +Y)
module hole_far(x, z, dia) {
  translate([wall+x, wall+in_y-eps, z]) rotate([-90,0,0]) cylinder(h=wall+2*eps, d=dia);
}

// ---------- корпус ----------
module body() {
  difference() {
    rbox(in_x+2*wall, in_y+2*wall, in_z+floor_t, r_out);
    translate([wall, wall, floor_t]) rbox(in_x, in_y, in_z+eps, r_out-1);

    hole_far(gland_in_x,  pen_z, pg9_d);        // ввод 220 из щитка
    hole_far(gland_out_x, pen_z, pg9_d);        // кабель к приводу

    // SMA радио 433 — правая стенка
    translate([wall+in_x-eps, wall+sma_right_y, sma_z])
      rotate([0,90,0]) cylinder(h=wall+2*eps, d=sma_d);
    // SMA GSM — левая стенка
    translate([-eps, wall+sma_left_y, sma_z])
      rotate([0,90,0]) cylinder(h=wall+2*eps, d=sma_d);
  }

  // стойки платы
  for (h = main_holes) translate([wall+h[0], wall+h[1], floor_t]) boss(standoff);

  // бобышки крышки по углам
  for (dx=[wall+5, wall+in_x-5], dy=[wall+5, wall+in_y-5])
    translate([dx, dy, floor_t]) boss(in_z);
}

// ---------- крышка ----------
module lid() {
  difference() {
    union() {
      rbox(in_x+2*wall, in_y+2*wall, lid_t, r_out);
      // фальц, входящий внутрь корпуса
      translate([wall+0.3, wall+0.3, -4])
        difference() {
          rbox(in_x-0.6, in_y-0.6, 4+eps, r_out-1);
          translate([2.5,2.5,-eps]) rbox(in_x-0.6-5, in_y-0.6-5, 4+3*eps, r_out-1);
        }
    }
    // канавка под силиконовый шнур по периметру фальца
    translate([wall+3.4, wall+3.4, -seal_deep])
      difference() {
        rbox(in_x-6.8, in_y-6.8, seal_deep+eps, 2);
        translate([seal_w, seal_w, -eps])
          rbox(in_x-6.8-2*seal_w, in_y-6.8-2*seal_w, seal_deep+3*eps, 2);
      }
    // винты крышки над бобышками
    for (dx=[wall+5, wall+in_x-5], dy=[wall+5, wall+in_y-5])
      translate([dx, dy, -eps]) {
        cylinder(h=lid_t+2*eps, d=m3_free);
        translate([0,0,lid_t-1.8]) cylinder(h=2, d=6.4);  // потай под головку
      }
  }
}

if (part == "body") body();
else if (part == "lid") lid();
else { body(); translate([0, in_y+2*wall+15, 0]) lid(); }
