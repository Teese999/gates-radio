// ============================================================================
// Умные Ворота — корпус под 3D-печать (этажерка)
// ============================================================================
// Компоновка (см. hardware/board/board.json):
//   нижний ярус — две протоплаты 6x8: БП RS-15-12 и колодки 220 В;
//   верхний ярус — основная плата 10x15 на стойках.
// Влагозащита: без вентиляции, крышка с фальцем и канавкой под силиконовый
// шнур 2 мм, кабели — только через гермовводы PG9, антенны — через SMA-гнёзда.
//
// Печать: PETG или ASA (на улице PLA плывёт), 4+ периметра, заполнение 25%+,
// дном вниз без поддержек. Резьба нигде не печатается: крышка и стойки — на
// латунных вставках M3 (запрессовать паяльником).
//
// Экспорт STL: openscad -o case_body.stl -D 'part="body"' case.scad
//              openscad -o case_lid.stl  -D 'part="lid"'  case.scad
// ============================================================================

part = "body"; // "body" | "lid" | "preview" (обе детали рядом)

// ---------- основные параметры, мм ----------
wall      = 3;      // стенка
floor_t   = 3;      // дно
lid_t     = 3;      // крышка
r_out     = 4;      // скругление углов

// Основная плата 10x15 наверху, нижний ярус — две платы 6x8 (80x60) бок о бок.
// Нижний ярус шире основной платы (160 мм против 150), поэтому внутреннюю
// ширину диктует он: 2 + 80 + 2 + 80 + 2 = 166.
main_x    = 150;
main_y    = 100;

in_x = 166;
in_y = 110;
// Высота: бобышки нижнего яруса 6 + плата 1.6 + БП 28 + зазор до верхней
// платы 8 (разводка снизу) + плата 1.6 + модули 17 + воздух до крышки 12
lower_boss = 6;
stack_gap  = 44;                        // низ основной платы над дном
in_z       = stack_gap + 1.6 + 17 + 12; // 74.6 -> печатаем 75

// Позиции плат во внутренних координатах (от левого нижнего внутреннего угла)
main_off  = [(in_x - main_x)/2, (in_y - main_y)/2];   // [8, 5]
pb_size   = [80, 60];
pb1_off   = [2,  (in_y - pb_size[1])/2];              // [2, 25]
pb2_off   = [84, (in_y - pb_size[1])/2];              // [84, 25]

// ---------- крепёж ----------
insert_d  = 4.0;    // отверстие под латунную вставку M3 (типовая ~4.6 наружн., натяг)
insert_h  = 6;
m3_free   = 3.4;

pb_hole   = [2.54*1, 2.54*1];           // угловое отверстие [2,2] от угла платы 6x8

// Стойки основной платы: отверстия [2,2],[57,2],[2,37],[57,37] шаг 2.54
main_holes = [ for (xy = [[1,1],[56,1],[1,36],[56,36]])
               [xy[0]*2.54 + main_off[0], xy[1]*2.54 + main_off[1]] ];

// ---------- отверстия в стенках ----------
// Держатель предохранителя (по чертежу пользователя): резьба M13,
// колпачок D15 снаружи ~13 мм, тело внутрь 31 мм + лепестки.
fuse_hole_d   = 13.2;
fuse_center_z = floor_t + 20;
fuse_center_x = pb2_off[0] + 40;        // нижняя стенка (Y=0), напротив платы колодок

// Гермовводы PG9: отверстие 15.2
pg9_d      = 15.2;
gland_z    = floor_t + 14;
gland1_x   = pb2_off[0] + 15;           // ввод 220 (нижняя стенка, у колодок)
gland2_y   = 40;                        // кабель привода (левая стенка)

// SMA-гнёзда: отверстие 6.5. Высота — уровень верхней платы.
sma_d      = 6.5;
sma_z      = stack_gap + 1.6 + 8;
sma_right_y = main_off[1] + (8-1)*2.54 + 8;   // CC1101, ряд 8 верхней платы (правая стенка)
sma_left_y  = main_off[1] + (9-1)*2.54 + 8;   // пигтейл GSM, ряд 9 (левая стенка)

// Уплотнение крышки: канавка под силиконовый шнур d2
seal_w = 2.6; seal_deep = 1.4;

$fn = 48;
eps = 0.01;

// ---------- заготовки ----------
module rbox(x, y, z, r) { // скруглённый в плане бокс
  hull() for (dx=[r, x-r], dy=[r, y-r]) translate([dx,dy,0]) cylinder(h=z, r=r);
}

module boss(h, hole_d) {
  difference() {
    cylinder(h=h, d=hole_d+5);
    translate([0,0,h-insert_h]) cylinder(h=insert_h+eps, d=insert_d);
  }
}

// ---------- корпус ----------
module body() {
  difference() {
    rbox(in_x+2*wall, in_y+2*wall, in_z+floor_t, r_out);
    translate([wall, wall, floor_t]) rbox(in_x, in_y, in_z+eps, r_out-1);

    // --- проёмы в стенках (координаты внутренние + wall) ---
    // держатель предохранителя: нижняя стенка (Y=0), зона 220
    translate([wall+fuse_center_x, -eps, fuse_center_z])
      rotate([-90,0,0]) cylinder(h=wall+2*eps, d=fuse_hole_d);
    // гермоввод ввода 220: нижняя стенка
    translate([wall+gland1_x, -eps, gland_z])
      rotate([-90,0,0]) cylinder(h=wall+2*eps, d=pg9_d);
    // гермоввод кабеля привода: левая стенка (X=0)
    translate([-eps, wall+gland2_y, gland_z])
      rotate([0,90,0]) cylinder(h=wall+2*eps, d=pg9_d);
    // SMA радио 433: правая стенка
    translate([wall+in_x-eps, wall+sma_right_y, sma_z])
      rotate([0,90,0]) cylinder(h=wall+2*eps, d=sma_d);
    // SMA GSM: левая стенка
    translate([-eps, wall+sma_left_y, sma_z])
      rotate([0,90,0]) cylinder(h=wall+2*eps, d=sma_d);
  }

  // --- бобышки нижнего яруса (две платы 6x8, по 4 угла) ---
  for (off = [pb1_off, pb2_off])
    for (dx = [pb_hole[0], pb_size[0]-pb_hole[0]], dy = [pb_hole[1], pb_size[1]-pb_hole[1]])
      translate([wall+off[0]+dx, wall+off[1]+dy, floor_t]) boss(lower_boss, insert_d);

  // --- стойки основной платы: от дна до stack_gap ---
  for (h = main_holes)
    translate([wall+h[0], wall+h[1], floor_t]) boss(stack_gap - floor_t, insert_d);

  // --- бобышки крышки по углам ---
  for (dx=[wall+5, wall+in_x-5], dy=[wall+5, wall+in_y-5])
    translate([dx, dy, floor_t]) boss(in_z, insert_d);
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
        translate([seal_w, seal_w, -eps]) rbox(in_x-6.8-2*seal_w, in_y-6.8-2*seal_w, seal_deep+3*eps, 2);
      }
    // винты крышки над бобышками
    for (dx=[wall+5, wall+in_x-5], dy=[wall+5, wall+in_y-5])
      translate([dx, dy, -eps]) {
        cylinder(h=lid_t+2*eps, d=m3_free);
        translate([0,0,lid_t-1.8]) cylinder(h=2, d=6.4); // потай под головку
      }
  }
}

if (part == "body") body();
else if (part == "lid") lid();
else { body(); translate([0, in_y+2*wall+15, 0]) lid(); }
