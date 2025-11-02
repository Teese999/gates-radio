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
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_12BIT = {
    "Nice FLO",          // name
    12,                  // bitCount
    0,                   // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Nice FLO 24-bit
const SubGhzProtocolConfig PROTOCOL_NICE_FLO_24BIT = {
    "Nice FLO",          // name
    24,                  // bitCount
    0,                   // te
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
const SubGhzProtocolConfig PROTOCOL_BYTEC = {
    "Bytec",             // name
    24,                  // bitCount
    0,                   // te
    1.0f,                // highRatio
    3.0f,                // lowRatio
    false,               // inverted
    false,               // manchester
    0,                   // preambleMin
    0,                   // preambleMax
    0.0f                 // preambleRatio
};

// Gate TX - из Flipper Zero: SUBGHZ_PROTOCOL_GATE_TX_NAME
const SubGhzProtocolConfig PROTOCOL_GATE_TX = {
    "Gate TX",           // name
    24,                  // bitCount
    0,                   // te
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

// Массив всех протоколов (в порядке приоритета - популярные первыми)
const SubGhzProtocolConfig* ALL_PROTOCOLS[] = {
    // CAME - приоритет для ворот
    &PROTOCOL_CAME_24BIT,
    &PROTOCOL_CAME_12BIT,
    
    // Princeton и варианты
    &PROTOCOL_PRINCETON,
    &PROTOCOL_BYTEC,
    &PROTOCOL_GATE_TX,
    
    // Nero протоколы (приоритет для Nero Radio 56-bit)
    &PROTOCOL_NERO_RADIO,    // 56-bit (более распространенный)
    &PROTOCOL_NERO_SKETCH,   // 24-bit
    
    // Nice FLO
    &PROTOCOL_NICE_FLO_24BIT,
    &PROTOCOL_NICE_FLO_12BIT,
    
    // Популярные протоколы
    &PROTOCOL_EV1527,
    &PROTOCOL_PT2262,        // Основной вариант 1:3
    &PROTOCOL_PT2262_1_2,    // Вариант 1:2
    &PROTOCOL_PT2262_1_1,    // Вариант 1:1
    &PROTOCOL_HX2262,
    &PROTOCOL_ROGER,
    &PROTOCOL_LINEAR,
    &PROTOCOL_BETT,
    &PROTOCOL_NERO_SKETCH,
    
    nullptr  // Маркер конца
};

const int PROTOCOL_COUNT = sizeof(ALL_PROTOCOLS) / sizeof(ALL_PROTOCOLS[0]) - 1;

