#include "SubGhzProtocols.h"

// CAME 12-bit (из Flipper Zero: SUBGHZ_PROTOCOL_CAME_NAME, 12 бит)
// TE: ~320-380 мкс, соотношение 1:3 (1 TE HIGH, 3 TE LOW для 0; 3 TE HIGH, 1 TE LOW для 1)
const SubGhzProtocolConfig PROTOCOL_CAME_12BIT = {
    "CAME",              // name
    12,                  // bitCount
    320.0f,             // te (типичное значение ~320 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// CAME 24-bit (из Flipper Zero: SUBGHZ_PROTOCOL_CAME_NAME, 24 бит)
// TE: ~320-380 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_CAME_24BIT = {
    "CAME",              // name
    24,                  // bitCount
    320.0f,             // te (типичное значение ~320 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nice FLO 12-bit
// TE: обычно ~400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_12BIT = {
    "Nice FLO",          // name
    12,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nice FLO 24-bit
// TE: обычно ~400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_24BIT = {
    "Nice FLO",          // name
    24,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Princeton (любой TE) - из Flipper Zero: SUBGHZ_PROTOCOL_PRINCETON_NAME
// TE: обычно ~400 мкс, соотношение 1:3 (1 TE HIGH, 3 TE LOW для 0; 3 TE HIGH, 1 TE LOW для 1)
const SubGhzProtocolConfig PROTOCOL_PRINCETON = {
    "Princeton",         // name
    24,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Bytec (Princeton вариант)
// TE: обычно ~400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_BYTEC = {
    "Bytec",             // name
    24,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Gate TX - из Flipper Zero: SUBGHZ_PROTOCOL_GATE_TX_NAME
// TE: обычно ~400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_GATE_TX = {
    "Gate TX",           // name
    24,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nero Sketch (24-bit)
const SubGhzProtocolConfig PROTOCOL_NERO_SKETCH = {
    "Nero Sketch",       // name
    24,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nero Radio (56-bit) - из Flipper Zero: SUBGHZ_PROTOCOL_NERO_RADIO_NAME
// TE: обычно ~300-400 мкс, соотношение 1:3
// Формат: 56 бит (7 байт), используется для систем управления воротами Nero
const SubGhzProtocolConfig PROTOCOL_NERO_RADIO = {
    "Nero Radio",        // name
    56,                  // bitCount (56 бит = 7 байт)
    330.0f,             // te (типичное значение ~330 мкс, основано на RAW данных)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// EV1527 (28-bit) - популярный протокол для дистанционных выключателей
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_EV1527 = {
    "EV1527",            // name
    28,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// PT2262 (24-bit) - популярный протокол для дистанционных выключателей
// TE: обычно ~400-600 мкс, соотношение 1:1, 1:2 или 1:3 (зависит от модели)
// Основной вариант - 1:3 (самый распространенный)
const SubGhzProtocolConfig PROTOCOL_PT2262 = {
    "PT2262",            // name
    24,                  // bitCount
    500.0f,             // te (типичное значение ~500 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio (чаще всего 1:3, но может быть 1:1 или 1:2)
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// PT2262 вариант с соотношением 1:1
const SubGhzProtocolConfig PROTOCOL_PT2262_1_1 = {
    "PT2262_1:1",        // name
    24,                  // bitCount
    500.0f,             // te
    1.0f,                // highRatio
    1.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// PT2262 вариант с соотношением 1:2
const SubGhzProtocolConfig PROTOCOL_PT2262_1_2 = {
    "PT2262_1:2",        // name
    24,                  // bitCount
    500.0f,             // te
    1.0f,                // highRatio
    2.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// HX2262 (32-bit) - вариант PT2262 с большим количеством бит
// TE: обычно ~400-500 мкс, соотношение 1:2 или 1:3
const SubGhzProtocolConfig PROTOCOL_HX2262 = {
    "HX2262",            // name
    32,                  // bitCount
    500.0f,             // te (типичное значение ~500 мкс)
    1.0f,                // highRatio
    2.0f,                // lowRatio (чаще 1:2, но может быть 1:3)
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Roger (28-bit) - из Flipper Zero: SUBGHZ_PROTOCOL_ROGER_NAME
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_ROGER = {
    "Roger",             // name
    28,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Linear (10-bit) - из Flipper Zero: SUBGHZ_PROTOCOL_LINEAR_NAME
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_LINEAR = {
    "Linear",            // name
    10,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// BETT (18-bit) - из Flipper Zero: SUBGHZ_PROTOCOL_BETT_NAME
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_BETT = {
    "BETT",              // name
    18,                  // bitCount
    400.0f,             // te (типичное значение ~400 мкс)
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nice FlorS (12-bit) - популярный протокол для ворот Nice
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_12BIT = {
    "Nice FlorS",        // name
    12,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nice FlorS (24-bit) - популярный протокол для ворот Nice
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_24BIT = {
    "Nice FlorS",        // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Holtek (12-bit) - популярный протокол
// TE: обычно ~300-400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_HOLTEK_12BIT = {
    "Holtek",            // name
    12,                  // bitCount
    350.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Holtek (24-bit)
// TE: обычно ~300-400 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_HOLTEK_24BIT = {
    "Holtek",            // name
    24,                  // bitCount
    350.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Marantec (24-bit) - протокол для ворот Marantec
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_MARANTEC = {
    "Marantec",          // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// FAAC SLH (24-bit) - протокол для ворот FAAC
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_FAAC_SLH = {
    "FAAC SLH",          // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Aprimatic (24-bit) - протокол для ворот Aprimatic
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_APRIMATIC = {
    "Aprimatic",         // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// AN-Motors (24-bit) - протокол для ворот AN-Motors
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_AN_MOTORS = {
    "AN-Motors",         // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// X10 (20-bit) - популярный протокол для умного дома
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_X10 = {
    "X10",               // name
    20,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Security+ 2.0 (24-bit) - протокол для гаражных ворот
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS_2 = {
    "Security+ 2.0",     // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Kia / Hyundai (24-bit) - протокол для автомобилей
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI = {
    "Kia/Hyundai",       // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Keeloq (64-bit) - криптографический протокол (базовая поддержка декодирования структуры)
// TE: обычно ~400-500 мкс, соотношение 1:3
// Примечание: KEELOQ требует криптографического декодирования, здесь только структура
const SubGhzProtocolConfig PROTOCOL_KEELOQ = {
    "Keeloq",            // name
    64,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Magellan (24-bit) - протокол для ворот
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_MAGELLAN = {
    "Magellan",          // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Star Line (24-bit) - протокол для автомобильных сигнализаций
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_STAR_LINE = {
    "Star Line",         // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// DoorHan (24-bit) - протокол для ворот DoorHan
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_DOORHAN = {
    "DoorHan",           // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// IDTec (24-bit) - протокол для ворот
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_IDTEC = {
    "IDTec",             // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Chambon (24-bit) - протокол для ворот Chambon
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_CHAMBON = {
    "Chambon",           // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Dooya (24-bit) - протокол для рольставней и ворот
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_DOOYA = {
    "Dooya",             // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Somfy (56-bit) - популярный протокол для рольставней и ворот Somfy
// TE: обычно ~640 мкс, соотношение 1:1 (манчестерское кодирование)
const SubGhzProtocolConfig PROTOCOL_SOMFY = {
    "Somfy",             // name
    56,                  // bitCount
    640.0f,             // te
    1.0f,                // highRatio
    1.0f,                // lowRatio (манчестер)
    false,               // inverted
    true,                // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// BFT (24-bit) - протокол для ворот BFT
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_BFT = {
    "BFT",               // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Security+ (24-bit) - протокол для гаражных ворот
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS = {
    "Security+",         // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// FAAC (24-bit) - протокол для ворот FAAC (альтернативный)
// TE: обычно ~400-500 мкс, соотношение 1:3
const SubGhzProtocolConfig PROTOCOL_FAAC = {
    "FAAC",              // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Kia / Hyundai (32-bit) - альтернативный вариант
const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI_32BIT = {
    "Kia/Hyundai",       // name
    32,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Linear (40-bit) - расширенный вариант Linear
const SubGhzProtocolConfig PROTOCOL_LINEAR_40BIT = {
    "Linear",            // name
    40,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Yale (24-bit) - протокол для замков Yale
const SubGhzProtocolConfig PROTOCOL_YALE = {
    "Yale",              // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Elro (24-bit) - протокол для сигнализаций Elro
const SubGhzProtocolConfig PROTOCOL_ELRO = {
    "Elro",              // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// HomeEasy (32-bit) - протокол для умного дома
const SubGhzProtocolConfig PROTOCOL_HOMEEASY = {
    "HomeEasy",          // name
    32,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Intertechno (24-bit) - протокол для умного дома
const SubGhzProtocolConfig PROTOCOL_INTERTECHNO = {
    "Intertechno",       // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// HID (26-bit) - протокол для систем контроля доступа HID
const SubGhzProtocolConfig PROTOCOL_HID = {
    "HID",               // name
    26,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Oregon Scientific (36-bit) - протокол для метеостанций
const SubGhzProtocolConfig PROTOCOL_OREGON = {
    "Oregon",            // name
    36,                  // bitCount
    500.0f,             // te
    1.0f,                // highRatio
    2.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Maestro (24-bit) - протокол для ворот
const SubGhzProtocolConfig PROTOCOL_MAESTRO = {
    "Maestro",           // name
    24,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Chamberlain (32-bit) - протокол для гаражных ворот Chamberlain
const SubGhzProtocolConfig PROTOCOL_CHAMBERLAIN = {
    "Chamberlain",       // name
    32,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Marantec (32-bit) - расширенный вариант Marantec
const SubGhzProtocolConfig PROTOCOL_MARANTEC_32BIT = {
    "Marantec",          // name
    32,                  // bitCount
    400.0f,             // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Массив всех протоколов (в порядке приоритета - популярные первыми)
// Порядок: сначала протоколы для ворот, затем популярные протоколы, затем остальные
const SubGhzProtocolConfig* ALL_PROTOCOLS[] = {
    // CAME - приоритет для ворот (работает отлично!)
    &PROTOCOL_CAME_24BIT,
    &PROTOCOL_CAME_12BIT,
    
    // Nice протоколы - очень популярны для ворот
    &PROTOCOL_NICE_FLORS_24BIT,
    &PROTOCOL_NICE_FLORS_12BIT,
    &PROTOCOL_NICE_FLO_24BIT,
    &PROTOCOL_NICE_FLO_12BIT,
    
    // Nero протоколы
    &PROTOCOL_NERO_RADIO,    // 56-bit
    &PROTOCOL_NERO_SKETCH,   // 24-bit
    
    // Princeton и варианты
    &PROTOCOL_PRINCETON,
    &PROTOCOL_BYTEC,
    &PROTOCOL_GATE_TX,
    
    // Популярные протоколы для ворот и шлагбаумов
    &PROTOCOL_MARANTEC,
    &PROTOCOL_MARANTEC_32BIT,
    &PROTOCOL_FAAC_SLH,
    &PROTOCOL_FAAC,
    &PROTOCOL_APRIMATIC,
    &PROTOCOL_AN_MOTORS,
    &PROTOCOL_DOORHAN,
    &PROTOCOL_IDTEC,
    &PROTOCOL_CHAMBON,
    &PROTOCOL_DOOYA,
    &PROTOCOL_MAGELLAN,
    &PROTOCOL_BFT,
    &PROTOCOL_MAESTRO,
    &PROTOCOL_CHAMBERLAIN,
    
    // Протоколы с манчестерским кодированием (Somfy)
    &PROTOCOL_SOMFY,
    
    // Популярные протоколы общего назначения
    &PROTOCOL_EV1527,
    &PROTOCOL_PT2262,        // Основной вариант 1:3
    &PROTOCOL_PT2262_1_2,    // Вариант 1:2
    &PROTOCOL_PT2262_1_1,    // Вариант 1:1
    &PROTOCOL_HX2262,
    &PROTOCOL_ROGER,
    &PROTOCOL_HOLTEK_24BIT,
    &PROTOCOL_HOLTEK_12BIT,
    
    // Протоколы для умного дома
    &PROTOCOL_X10,
    &PROTOCOL_HOMEEASY,
    &PROTOCOL_INTERTECHNO,
    &PROTOCOL_ELRO,
    
    // Протоколы безопасности
    &PROTOCOL_SECURITY_PLUS,
    &PROTOCOL_SECURITY_PLUS_2,
    &PROTOCOL_YALE,
    &PROTOCOL_HID,
    
    // Автомобильные протоколы
    &PROTOCOL_STAR_LINE,
    &PROTOCOL_KIA_HYUNDAI,
    &PROTOCOL_KIA_HYUNDAI_32BIT,
    
    // Другие протоколы
    &PROTOCOL_LINEAR,
    &PROTOCOL_LINEAR_40BIT,
    &PROTOCOL_BETT,
    &PROTOCOL_OREGON,
    
    // Длинные протоколы (в конце, так как требуют больше времени на декодирование)
    &PROTOCOL_KEELOQ,        // 64-bit (криптографический, только структура)
    
    nullptr  // Маркер конца
};

const int PROTOCOL_COUNT = sizeof(ALL_PROTOCOLS) / sizeof(ALL_PROTOCOLS[0]) - 1;

