#ifndef SUBGHZ_PROTOCOLS_H
#define SUBGHZ_PROTOCOLS_H

#include <Arduino.h>

// Структура для конфигурации протокола (адаптировано из Flipper Zero)
struct SubGhzProtocolConfig {
    const char* name;           // Имя протокола
    int bitCount;               // Количество бит
    float te;                   // Базовый период (0 = автоопределение)
    float highRatio;            // Отношение HIGH к TE
    float lowRatio;             // Отношение LOW к TE
    bool inverted;              // Инвертирован ли протокол
    bool manchester;            // Использует ли манчестерское кодирование
    int preambleMin;            // Минимальная длина преамбулы (в импульсах)
    int preambleMax;            // Максимальная длина преамбулы
    float preambleRatio;       // Соотношение преамбулы (если специфичное)
};

// Список основных протоколов из Flipper Zero
// Статические коды (для ворот)
extern const SubGhzProtocolConfig PROTOCOL_CAME_12BIT;
extern const SubGhzProtocolConfig PROTOCOL_CAME_24BIT;
extern const SubGhzProtocolConfig PROTOCOL_NICE_FLO_12BIT;
extern const SubGhzProtocolConfig PROTOCOL_NICE_FLO_24BIT;
extern const SubGhzProtocolConfig PROTOCOL_PRINCETON;
extern const SubGhzProtocolConfig PROTOCOL_BYTEC;
extern const SubGhzProtocolConfig PROTOCOL_GATE_TX;
extern const SubGhzProtocolConfig PROTOCOL_NERO_SKETCH;
extern const SubGhzProtocolConfig PROTOCOL_NERO_RADIO;

// Популярные протоколы
extern const SubGhzProtocolConfig PROTOCOL_EV1527;
extern const SubGhzProtocolConfig PROTOCOL_PT2262;
extern const SubGhzProtocolConfig PROTOCOL_PT2262_1_1;  // Вариант с соотношением 1:1
extern const SubGhzProtocolConfig PROTOCOL_PT2262_1_2;  // Вариант с соотношением 1:2
extern const SubGhzProtocolConfig PROTOCOL_HX2262;
extern const SubGhzProtocolConfig PROTOCOL_ROGER;
extern const SubGhzProtocolConfig PROTOCOL_LINEAR;
extern const SubGhzProtocolConfig PROTOCOL_BETT;

// Массив всех протоколов для перебора
extern const SubGhzProtocolConfig* ALL_PROTOCOLS[];
extern const int PROTOCOL_COUNT;

#endif // SUBGHZ_PROTOCOLS_H

