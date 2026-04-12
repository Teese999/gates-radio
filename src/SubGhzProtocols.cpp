#include "SubGhzProtocols.h"

// =============================================================================
// Протоколы SubGhz — дедуплицированные по реальным параметрам модуляции.
//
// Проблема: в оригинале 30+ протоколов имели одинаковые ratio (1:3) и TE (~400),
// отличаясь только именем. Декодер не мог их отличить — один сигнал детектился
// то как "Nero", то как "Linear", то как "X10".
//
// Решение: оставляем только уникальные комбинации (bitCount + ratio + TE).
// Протоколы с одинаковой модуляцией объединены. Для идентификации бренда
// нужна преамбула — пока не реализована, поэтому честно пишем "OOK_1:3"
// вместо угадывания бренда.
//
// Приоритет: больше бит → точнее распознавание → выше в списке.
// =============================================================================

// --- OOK 1:3 (самая распространённая модуляция для ворот) ---
// CAME, Princeton, Nice FLO, Nero Sketch, Gate TX, BFT, Marantec, AN-Motors,
// DoorHan, Aprimatic, Chambon, Dooya, Maestro, X10, etc.

// Keeloq — 64 бита, rolling code
const SubGhzProtocolConfig PROTOCOL_KEELOQ = {
    "Keeloq", 64, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// Nero Radio — 56 бит, статический
const SubGhzProtocolConfig PROTOCOL_NERO_RADIO = {
    "Nero Radio", 56, 330.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// Linear 40-bit
const SubGhzProtocolConfig PROTOCOL_LINEAR_40BIT = {
    "Linear 40", 40, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// 32-bit OOK 1:3 (Chamberlain, HomeEasy, Marantec 32, Kia)
const SubGhzProtocolConfig PROTOCOL_CHAMBERLAIN = {
    "OOK32", 32, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// EV1527 — 28 бит
const SubGhzProtocolConfig PROTOCOL_EV1527 = {
    "EV1527", 28, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// HID — 26 бит
const SubGhzProtocolConfig PROTOCOL_HID = {
    "HID", 26, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// CAME 24-bit (TE=320) — самый популярный для ворот
const SubGhzProtocolConfig PROTOCOL_CAME_24BIT = {
    "CAME", 24, 320.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// Princeton/Generic 24-bit (TE=400)
const SubGhzProtocolConfig PROTOCOL_PRINCETON = {
    "Princeton", 24, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// CAME 12-bit
const SubGhzProtocolConfig PROTOCOL_CAME_12BIT = {
    "CAME 12", 12, 320.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// Nice FLO 12-bit (TE=400)
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_12BIT = {
    "Nice FLO 12", 12, 400.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// Holtek 12-bit (TE=350)
const SubGhzProtocolConfig PROTOCOL_HOLTEK_12BIT = {
    "Holtek 12", 12, 350.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// --- OOK 1:2 ---

// Oregon Scientific — 36 бит
const SubGhzProtocolConfig PROTOCOL_OREGON = {
    "Oregon", 36, 500.0f, 1.0f, 2.0f, false, false, 0, 0, 0.0f
};

// HX2262 — 32 бит
const SubGhzProtocolConfig PROTOCOL_HX2262 = {
    "HX2262", 32, 500.0f, 1.0f, 2.0f, false, false, 0, 0, 0.0f
};

// PT2262 1:2 — 24 бит
const SubGhzProtocolConfig PROTOCOL_PT2262_1_2 = {
    "PT2262 1:2", 24, 500.0f, 1.0f, 2.0f, false, false, 0, 0, 0.0f
};

// --- OOK 1:1 ---

// PT2262 1:1 — 24 бит
const SubGhzProtocolConfig PROTOCOL_PT2262_1_1 = {
    "PT2262 1:1", 24, 500.0f, 1.0f, 1.0f, false, false, 0, 0, 0.0f
};

// --- Manchester ---

// Somfy — 56 бит
const SubGhzProtocolConfig PROTOCOL_SOMFY = {
    "Somfy", 56, 640.0f, 1.0f, 1.0f, false, true, 0, 0, 0.0f
};

// --- PT2262 base 1:3 (TE=500, чуть другой от Princeton TE=400) ---
const SubGhzProtocolConfig PROTOCOL_PT2262 = {
    "PT2262", 24, 500.0f, 1.0f, 3.0f, false, false, 0, 0, 0.0f
};

// =============================================================================
// Заглушки для ссылок из хедера (чтобы не менять .h файл)
// =============================================================================
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_24BIT = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_12BIT = PROTOCOL_NICE_FLO_12BIT;
const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_24BIT = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_BYTEC = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_GATE_TX = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_NERO_SKETCH = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_HOLTEK_24BIT = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_LINEAR = PROTOCOL_NICE_FLO_12BIT; // 10-bit → 12-bit fallback
const SubGhzProtocolConfig PROTOCOL_BETT = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_ROGER = PROTOCOL_EV1527;
const SubGhzProtocolConfig PROTOCOL_MARANTEC = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_MARANTEC_32BIT = PROTOCOL_CHAMBERLAIN;
const SubGhzProtocolConfig PROTOCOL_FAAC_SLH = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_FAAC = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_APRIMATIC = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_AN_MOTORS = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_DOORHAN = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_IDTEC = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_CHAMBON = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_DOOYA = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_MAGELLAN = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_BFT = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_MAESTRO = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS_2 = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_YALE = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_X10 = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_HOMEEASY = PROTOCOL_CHAMBERLAIN;
const SubGhzProtocolConfig PROTOCOL_INTERTECHNO = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_ELRO = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_STAR_LINE = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI = PROTOCOL_PRINCETON;
const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI_32BIT = PROTOCOL_CHAMBERLAIN;

// Массив протоколов — только уникальные, в порядке приоритета:
// Длинные (точнее) → короткие (быстрее).
// Внутри одного bitCount — специфичные TE первыми.
const SubGhzProtocolConfig* ALL_PROTOCOLS[] = {
    // 64-bit
    &PROTOCOL_KEELOQ,

    // 56-bit
    &PROTOCOL_SOMFY,         // Manchester, уникальный
    &PROTOCOL_NERO_RADIO,    // OOK 1:3, TE=330

    // 40-bit
    &PROTOCOL_LINEAR_40BIT,

    // 36-bit
    &PROTOCOL_OREGON,        // OOK 1:2

    // 32-bit
    &PROTOCOL_HX2262,        // OOK 1:2, TE=500
    &PROTOCOL_CHAMBERLAIN,   // OOK 1:3, TE=400

    // 28-bit
    &PROTOCOL_EV1527,

    // 26-bit
    &PROTOCOL_HID,

    // 24-bit (разные ratio и TE)
    &PROTOCOL_CAME_24BIT,    // OOK 1:3, TE=320
    &PROTOCOL_PT2262,        // OOK 1:3, TE=500
    &PROTOCOL_PRINCETON,     // OOK 1:3, TE=400 (generic fallback)
    &PROTOCOL_PT2262_1_2,    // OOK 1:2, TE=500
    &PROTOCOL_PT2262_1_1,    // OOK 1:1, TE=500

    // 12-bit
    &PROTOCOL_CAME_12BIT,    // OOK 1:3, TE=320
    &PROTOCOL_HOLTEK_12BIT,  // OOK 1:3, TE=350
    &PROTOCOL_NICE_FLO_12BIT,// OOK 1:3, TE=400

    nullptr
};

const int PROTOCOL_COUNT = sizeof(ALL_PROTOCOLS) / sizeof(ALL_PROTOCOLS[0]) - 1;
