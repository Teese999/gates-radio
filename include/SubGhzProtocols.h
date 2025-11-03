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

// Nice протоколы
extern const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_12BIT;
extern const SubGhzProtocolConfig PROTOCOL_NICE_FLORS_24BIT;

// Популярные протоколы
extern const SubGhzProtocolConfig PROTOCOL_EV1527;
extern const SubGhzProtocolConfig PROTOCOL_PT2262;
extern const SubGhzProtocolConfig PROTOCOL_PT2262_1_1;  // Вариант с соотношением 1:1
extern const SubGhzProtocolConfig PROTOCOL_PT2262_1_2;  // Вариант с соотношением 1:2
extern const SubGhzProtocolConfig PROTOCOL_HX2262;
extern const SubGhzProtocolConfig PROTOCOL_ROGER;
extern const SubGhzProtocolConfig PROTOCOL_LINEAR;
extern const SubGhzProtocolConfig PROTOCOL_BETT;

// Протоколы для ворот
extern const SubGhzProtocolConfig PROTOCOL_HOLTEK_12BIT;
extern const SubGhzProtocolConfig PROTOCOL_HOLTEK_24BIT;
extern const SubGhzProtocolConfig PROTOCOL_MARANTEC;
extern const SubGhzProtocolConfig PROTOCOL_MARANTEC_32BIT;
extern const SubGhzProtocolConfig PROTOCOL_FAAC_SLH;
extern const SubGhzProtocolConfig PROTOCOL_FAAC;
extern const SubGhzProtocolConfig PROTOCOL_APRIMATIC;
extern const SubGhzProtocolConfig PROTOCOL_AN_MOTORS;
extern const SubGhzProtocolConfig PROTOCOL_DOORHAN;
extern const SubGhzProtocolConfig PROTOCOL_IDTEC;
extern const SubGhzProtocolConfig PROTOCOL_CHAMBON;
extern const SubGhzProtocolConfig PROTOCOL_DOOYA;
extern const SubGhzProtocolConfig PROTOCOL_MAGELLAN;
extern const SubGhzProtocolConfig PROTOCOL_BFT;
extern const SubGhzProtocolConfig PROTOCOL_MAESTRO;
extern const SubGhzProtocolConfig PROTOCOL_CHAMBERLAIN;

// Протоколы с манчестерским кодированием
extern const SubGhzProtocolConfig PROTOCOL_SOMFY;

// Протоколы для умного дома
extern const SubGhzProtocolConfig PROTOCOL_HOMEEASY;
extern const SubGhzProtocolConfig PROTOCOL_INTERTECHNO;
extern const SubGhzProtocolConfig PROTOCOL_ELRO;

// Протоколы безопасности
extern const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS;
extern const SubGhzProtocolConfig PROTOCOL_SECURITY_PLUS_2;
extern const SubGhzProtocolConfig PROTOCOL_YALE;
extern const SubGhzProtocolConfig PROTOCOL_HID;

// Другие протоколы
extern const SubGhzProtocolConfig PROTOCOL_X10;
extern const SubGhzProtocolConfig PROTOCOL_LINEAR_40BIT;
extern const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI_32BIT;
extern const SubGhzProtocolConfig PROTOCOL_STAR_LINE;
extern const SubGhzProtocolConfig PROTOCOL_KIA_HYUNDAI;
extern const SubGhzProtocolConfig PROTOCOL_OREGON;
extern const SubGhzProtocolConfig PROTOCOL_KEELOQ;  // 64-bit криптографический

// Массив всех протоколов для перебора
extern const SubGhzProtocolConfig* ALL_PROTOCOLS[];
extern const int PROTOCOL_COUNT;

#endif // SUBGHZ_PROTOCOLS_H

