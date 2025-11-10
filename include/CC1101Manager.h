#ifndef CC1101MANAGER_H
#define CC1101MANAGER_H

#include <Arduino.h>
#include <RadioLib.h>
#include "SubGhzProtocols.h"

// Типы модуляции (как в Flipper Zero)
enum ModulationType {
    MODULATION_ASK_OOK = 0,  // ASK/OOK (амплитудная модуляция)
    MODULATION_FSK_2FSK,     // FSK (частотная модуляция, 2-FSK)
    MODULATION_MSK,          // MSK (минимальная манипуляция)
    MODULATION_GFSK          // GFSK (Gaussian FSK)
};

// Структура для хранения принятого ключа
struct ReceivedKey {
    bool available;
    String rawData;           // RAW данные (для отображения)
    String bitString;        // Битовые строки (01010101...) для точного сравнения
    int bitLength;           // Количество бит
    float te;                // Базовый период (Time Element) в мкс
    uint32_t hash;           // Хеш сигнала (для устранения дубликатов)
    uint32_t code;
    int rssi;
    float snr;
    float frequencyError;
    unsigned long timestamp;
    uint8_t dataLength;
    String protocol;
    String modulation;
};

class CC1101Manager {
public:
    // Инициализация CC1101
    static bool init(int csPin, int gdo0Pin, int gdo2Pin);
    
    // Установка частоты (в МГц)
    static bool setFrequency(float freq);
    
    // Получение текущей частоты
    static float getFrequency();
    
    // Установка модуляции (для будущего использования)
    static bool setModulation(ModulationType mod);
    
    // Получение текущей модуляции
    static ModulationType getModulation();
    
    // Начать прослушивание
    static bool startReceive();
    
    // Проверить наличие принятых данных
    static bool checkReceived();
    
    // Получить принятый ключ
    static ReceivedKey getReceivedKey();
    
    // Сброс принятых данных
    static void resetReceived();
    
    // Получить RSSI
    static int getRSSI();
    
    // Установить битрейт (bps)
    static bool setBitRate(float br);
    
    // Установить девиацию частоты (кГц)
    static bool setFrequencyDeviation(float freqDev);
    
    // Установить ширину полосы приемника (кГц)
    static bool setRxBandwidth(float rxBw);
    
    // Вывод информации о конфигурации
    static void printConfig();
    
    // Получить счетчик прерываний (для отладки)
    static unsigned long getInterruptCount();
    
    // Callback для обработки прерывания
    static void IRAM_ATTR onInterrupt();

private:
    static CC1101* radio;
    static float currentFrequency;
    static ModulationType currentModulation;
    static volatile bool receivedFlag;
    static ReceivedKey lastKey;
    static int gdo0PinNumber;

    // Работа в RAW (direct) режиме
    static bool configureForRawMode();
    static bool enterRawReceive();
    static void attachRawInterrupt();
    static void detachRawInterrupt();
    static void resetRawBuffer();
    static bool signalLooksValid(int pulseCount);
    static uint32_t computeHash(const volatile unsigned long* timings, int length);
    static bool analyzePulsePattern(int pulseCount, float& estimatedTe);
    static bool decodeWithProtocols(int pulseCount, float te, uint32_t& codeOut, String& protocolName, String& bitStringOut);

    // Буфер и состояние RAW импульсов
    static const int MAX_RAW_SIGNAL_LENGTH = 1024;
    static volatile unsigned long rawSignalTimings[MAX_RAW_SIGNAL_LENGTH];
    static volatile bool rawSignalLevels[MAX_RAW_SIGNAL_LENGTH];
    static volatile int rawSignalIndex;
    static volatile unsigned long lastInterruptTime;
    static volatile bool rawSignalReady;
    static volatile unsigned long interruptCounter;
    static volatile bool firstEdgeCaptured;
    static volatile bool lastSignalLevel;
    static unsigned long lastDetectionTime;
    static uint32_t lastDetectionHash;
    static uint32_t lastDetectionCode;  // Последний декодированный код
    static String lastDetectionProtocol; // Последний протокол
    static int duplicateCount;           // Счетчик повторений
    static unsigned long initTime;      // Время инициализации для фильтрации начальных сигналов
    static uint32_t lastFullDecodedCode; // Последний полностью декодированный код (24/24 бита)
    static unsigned long lastFullDecodedTime; // Время последнего полного декодирования

    // Структуры протоколов (адаптация RCSwitch)
    struct PulsePattern {
        bool level;
        unsigned long duration;
    };

    struct DecodedResult {
        bool success;
        uint32_t code;
        int bitLength;
        String protocol;
        String bitString;
    };

    static DecodedResult tryDecodeKnownProtocols(const PulsePattern* pulses, int length);
    static bool decodeProtocolRCSwitch(const PulsePattern* pulses, int length, float baseDelay,
                                       float highRatio, float lowRatio, bool inverted,
                                       int bitCount, const char* protocolName,
                                       const SubGhzProtocolConfig* protoConfig,
                                       DecodedResult& out);
    
    // Вспомогательная функция для определения TE из сигнала (как в Flipper Zero)
    static float findBestTE(const PulsePattern* pulses, int length, float initialTE);
};

#endif // CC1101MANAGER_H

