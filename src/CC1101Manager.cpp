#include "CC1101Manager.h"
#include "SubGhzProtocols.h"
#include "SubGhzDecoder.h"
#include <math.h>
#include <algorithm>

// Subclass CC1101 to access protected SPI register methods
class CC1101Ex : public CC1101 {
public:
    using CC1101::CC1101;
    int16_t writeReg(uint8_t reg, uint8_t value) { return SPIsetRegValue(reg, value); }
    void writeRegDirect(uint8_t reg, uint8_t data) { SPIwriteRegister(reg, data); }
    uint8_t getRegValue(uint8_t reg) { return SPIreadRegister(reg); }
};

// Глобальный мультидекодер (Flipper Zero architecture)
static SubGhzMultiDecoder multiDecoder;

// Ring buffer for ISR -> main loop pulse streaming
// ISR writes pulse data, main loop reads and feeds decoders
static constexpr int RING_BUF_SIZE = 1024;
static volatile unsigned long ringTimings[RING_BUF_SIZE];
static volatile bool ringLevels[RING_BUF_SIZE];
static volatile int ringWriteIdx = 0;
static int ringReadIdx = 0; // Only read from main loop

// RT decoder result
static volatile bool rtDecoderReady = false;
static volatile uint64_t rtDecoderData = 0;
static volatile int rtDecoderBits = 0;
static volatile float rtDecoderTe = 0;
static const char* volatile rtDecoderProtocol = nullptr;

namespace {
    constexpr unsigned long MIN_PULSE_US = 200;
    constexpr unsigned long MAX_PULSE_US = 30000; // CAME preamble ~18000, Nice FLO ~25200
    // DEBUG: логируем первый буфер для анализа
    static bool debugFirstBuffer = true;
    constexpr unsigned long END_GAP_US = 28000; // > Nice FLO preamble (25200), < MAX_PULSE (30000)
    constexpr unsigned long GLUE_THRESHOLD_US = 40;
    constexpr int MIN_PULSES_TO_ACCEPT = 40;
    constexpr unsigned long DUPLICATE_SUPPRESS_MS = 3000; // Для RAW сигналов
    constexpr unsigned long DECODED_DUPLICATE_SUPPRESS_MS = 5000; // Для декодированных протоколов (5 секунд)
    // RSSI threshold отключен - как во Flipper Zero, фильтрация по RSSI не используется
    constexpr float TE_VARIANCE_LIMIT = 0.25f;
    constexpr int MIN_VALID_BITS = 12; // Минимум бит для валидного протокола (отфильтровываем код 0)
    constexpr int MIN_SIGNAL_LENGTH = 40; // Минимум переходов для валидного сигнала
    constexpr int MIN_RAW_SIGNAL_LENGTH = 40; // Минимум переходов для RAW сигнала
    constexpr float MIN_PATTERN_CONFIDENCE = 0.5f; // Минимум уверенности в наличии паттерна (50% импульсов должны группироваться)
}

// Статические переменные
CC1101* CC1101Manager::radio = nullptr;
float CC1101Manager::currentFrequency = 433.92; // Фиксированная частота для fixed scan
ModulationType CC1101Manager::currentModulation = MODULATION_ASK_OOK;
volatile bool CC1101Manager::receivedFlag = false;
ReceivedKey CC1101Manager::lastKey;
int CC1101Manager::gdo0PinNumber = -1;

// Буферы RAW сигнала
volatile unsigned long CC1101Manager::rawSignalTimings[CC1101Manager::MAX_RAW_SIGNAL_LENGTH];
volatile bool CC1101Manager::rawSignalLevels[CC1101Manager::MAX_RAW_SIGNAL_LENGTH];
volatile int CC1101Manager::rawSignalIndex = 0;
volatile unsigned long CC1101Manager::lastInterruptTime = 0;
volatile bool CC1101Manager::rawSignalReady = false;
volatile unsigned long CC1101Manager::interruptCounter = 0;
volatile bool CC1101Manager::firstEdgeCaptured = false;
volatile bool CC1101Manager::lastSignalLevel = false;
unsigned long CC1101Manager::lastDetectionTime = 0;
uint32_t CC1101Manager::lastDetectionHash = 0;
uint32_t CC1101Manager::lastDetectionCode = 0;
String CC1101Manager::lastDetectionProtocol = "";
int CC1101Manager::duplicateCount = 0;
uint32_t CC1101Manager::lastFullDecodedCode = 0; // Последний полностью декодированный код (24/24 бита)
unsigned long CC1101Manager::lastFullDecodedTime = 0; // Время последнего полного декодирования
unsigned long CC1101Manager::initTime = 0; // Время инициализации для фильтрации начальных сигналов

// Вспомогательные функции RAW режима
bool CC1101Manager::configureForRawMode() {
    if (!radio) return false;

    CC1101* cc = static_cast<CC1101*>(radio);

    // Переводим модуль в standby перед перенастройкой
    int16_t state = cc->standby();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ❌ Не удалось перевести в standby перед RAW");
        return false;
    }
    
    // GDO0 -> Serial Data Async Output (0x0D) — как в Flipper Zero
    state = cc->setDIOMapping(0, RADIOLIB_CC1101_GDOX_SERIAL_DATA_ASYNC);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ❌ Ошибка назначения GDO0 для RAW");
        return false;
    }

    state = cc->setDIOMapping(2, RADIOLIB_CC1101_GDOX_SERIAL_CLOCK);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ⚠️ Не удалось назначить GDO2 CLOCK (продолжаем без него)");
    }

    // Включаем "промискуитет" — отключаем фильтры пакетов
    cc->setPromiscuousMode(true);

    // Выключаем CRC, автоперезапись — доступно через API setPacketMode?
    // Радиолиб автоматически выставит нужные параметры при receiveDirect(false).

    return true;
}

bool CC1101Manager::enterRawReceive() {
    if (!radio) return false;
    CC1101* cc = static_cast<CC1101*>(radio);

    resetRawBuffer();

    // Включаем прямой прием без синхронизации (async)
    int16_t state = cc->receiveDirectAsync();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] ❌ Ошибка запуска direct receive: ");
        Serial.println(state);
        return false;
    }

    attachRawInterrupt();
    Serial.println("[CC1101] 📡 Direct RAW прием активирован");
    return true;
}

void CC1101Manager::attachRawInterrupt() {
    if (gdo0PinNumber < 0) return;
    pinMode(gdo0PinNumber, INPUT);
    attachInterrupt(digitalPinToInterrupt(gdo0PinNumber), onInterrupt, CHANGE);
}

void CC1101Manager::detachRawInterrupt() {
    if (gdo0PinNumber < 0) return;
    detachInterrupt(digitalPinToInterrupt(gdo0PinNumber));
}

void CC1101Manager::resetRawBuffer() {
    rawSignalIndex = 0;
    rawSignalReady = false;
    interruptCounter = 0;
    lastInterruptTime = micros();
    firstEdgeCaptured = false;
    receivedFlag = false;
    lastSignalLevel = false;
}

bool CC1101Manager::signalLooksValid(int pulseCount) {
    // С Flipper-декодерами достаточно минимальной валидации —
    // декодеры сами отфильтруют шум через преамбулу и структуру протокола
    if (pulseCount < MIN_SIGNAL_LENGTH) {
        return false;
    }
    // Убрана проверка разброса (maxPulse/average) — преамбулы CAME(18000мкс) и
    // Nice FLO(25200мкс) имеют огромный разброс с данными(200-700мкс), это нормально.
    // Убрана проверка pattern grouping — Flipper-декодеры определяют паттерн сами.
    return true;
}

uint32_t CC1101Manager::computeHash(const volatile unsigned long* timings, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint32_t)timings[i];
        hash *= 16777619u;
    }
    return hash;
}

bool CC1101Manager::analyzePulsePattern(int pulseCount, float& estimatedTe) {
    if (pulseCount < 10) return false;
    
    // Используем более точный метод определения базового периода
    // Анализируем все импульсы для поиска наиболее часто встречающегося периода
    // Уменьшен размер массива для экономии стека
    const int sampleCount = min(pulseCount, 100);
    static unsigned long samples[100]; // Статический массив вместо локального
    for (int i = 0; i < sampleCount; i++) {
        samples[i] = rawSignalTimings[i];
    }
    
    // Сортируем для поиска медианы
    std::sort(samples, samples + sampleCount);
    
    // Используем медиану как начальную оценку
    estimatedTe = samples[sampleCount / 2];
    
    // Проверяем разумные границы
    if (estimatedTe < 100 || estimatedTe > 2000) {
        return false;
    }
    
    // Уточняем оценку: находим наиболее часто встречающийся период
    // Группируем похожие периоды
    float bestTe = estimatedTe;
    int bestCount = 0;
    
    for (int i = 0; i < sampleCount; i++) {
        float testTe = static_cast<float>(samples[i]);
        if (testTe < 100 || testTe > 2000) continue;
        
        int count = 0;
        for (int j = 0; j < sampleCount; j++) {
            float ratio = static_cast<float>(samples[j]) / testTe;
            float nearest = roundf(ratio);
            if (nearest < 0.5f) nearest = 0.5f;
            float diff = fabsf(ratio - nearest);
            if (diff < 0.3f) { // Толерантность 30%
                count++;
            }
        }
        
        if (count > bestCount) {
            bestCount = count;
            bestTe = testTe;
        }
    }
    
    estimatedTe = bestTe;
    
    // Проверяем качество сигнала с более строгими требованиями
    int validCount = 0;
    float totalDeviation = 0.0f;
    
    for (int i = 0; i < pulseCount; i++) {
        float ratio = static_cast<float>(rawSignalTimings[i]) / estimatedTe;
        float nearest = roundf(ratio);
        if (nearest < 0.5f) nearest = 0.5f;
        float diff = fabsf(ratio - nearest);
        if (diff < 0.3f) { // Ужесточено до 30% вместо 35%
            totalDeviation += diff;
            validCount++;
        }
    }
    
    // Требуем 60% валидных импульсов (ужесточено для лучшей фильтрации шумов)
    if (validCount < pulseCount * 0.6f) {
        return false; // Слишком много несоответствий
    }
    
    float avgDeviation = totalDeviation / validCount;
    
    // Проверка отклонения - 20% для лучшей фильтрации шумов
    if (avgDeviation > 0.20f) {
        return false;
    }
    
    return true;
}

CC1101Manager::DecodedResult CC1101Manager::tryDecodeKnownProtocols(const PulsePattern* pulses, int length) {
    DecodedResult bestResult {false, 0, 0, "", ""};
    
    // Определяем базовый период (TE) из сигнала (как в Flipper Zero)
    // Используем функцию findBestTE для более точного определения
    float estimatedTe = CC1101Manager::findBestTE(pulses, length, 0);
    
    // Если не удалось определить TE, используем медиану
    if (estimatedTe <= 0) {
        const int sampleCount = min(length, 100);
        static unsigned long samples[100];
        for (int i = 0; i < sampleCount; i++) {
            samples[i] = pulses[i].duration;
        }
        std::sort(samples, samples + sampleCount);
        estimatedTe = samples[sampleCount / 2];
    }
    
    // Алгоритм автоопределения протокола (как в Flipper Zero "read fixed scan")
    // 1. Пробуем ВСЕ протоколы из списка Flipper Zero
    // 2. Для каждого протокола тестируем разные варианты кодирования (инверсия, соотношения)
    // 3. Автоматически определяем оптимальный TE из сигнала
    // 4. Выбираем ЛУЧШИЙ результат по качеству декодирования (приоритет полному декодированию)
    // 5. Поддерживаем манчестерское кодирование для специальных протоколов (Somfy и др.)
    // 6. Не возвращаемся сразу после первого успешного - пробуем все протоколы и выбираем лучший
    
    // Используем протоколы из SubGhzProtocols (адаптированные из Flipper Zero)
    // Пробуем каждый протокол с разными вариантами кодирования
    
    int bestBits = 0;
    float bestQuality = 0.0f;
    bool hasFullDecode = false;
    
    // Проходим по всем протоколам из Flipper Zero
    for (int protoIdx = 0; protoIdx < PROTOCOL_COUNT; protoIdx++) {
        const SubGhzProtocolConfig* proto = ALL_PROTOCOLS[protoIdx];
        if (!proto) break;
        
        // Для каждого протокола пробуем разные варианты кодирования
        struct Variant {
            float highRatio;
            float lowRatio;
            bool inverted;
        };
        
        // Для протоколов с возможными разными соотношениями (например, PT2262)
        // используем только базовые варианты, так как варианты уже есть в списке протоколов
        Variant variants[3];
        int variantCount;
        
        if (strcmp(proto->name, "PT2262") == 0 || strcmp(proto->name, "PT2262_1:1") == 0 || 
            strcmp(proto->name, "PT2262_1:2") == 0) {
            // Для PT2262 уже есть отдельные варианты в списке протоколов
            // Используем только основной вариант + инвертированный
            variants[0] = {proto->highRatio, proto->lowRatio, proto->inverted};
            variants[1] = {proto->highRatio, proto->lowRatio, !proto->inverted};
            variantCount = 2;
        } else {
            // Для других протоколов пробуем разные варианты кодирования
            variants[0] = {proto->highRatio, proto->lowRatio, proto->inverted};
            variants[1] = {proto->lowRatio, proto->highRatio, proto->inverted};  // Обратное соотношение
            variants[2] = {proto->highRatio, proto->lowRatio, !proto->inverted}; // Инвертированный
            variantCount = 3;
        }
        
        for (int v = 0; v < variantCount; v++) {
            const auto& variant = variants[v];
            float baseDelay = (proto->te > 0) ? proto->te : estimatedTe;
            
            DecodedResult candidate {false, 0, 0, "", ""};
            if (decodeProtocolRCSwitch(pulses, length, baseDelay, variant.highRatio, variant.lowRatio,
                                       variant.inverted, proto->bitCount, proto->name, proto, candidate)) {
                // Оцениваем качество декодирования
                bool isFullDecode = (candidate.bitLength == proto->bitCount);
                float quality = static_cast<float>(candidate.bitLength) / proto->bitCount;
                
                // Приоритет полному декодированию
                bool isBetter = false;
                
                if (isFullDecode && !hasFullDecode) {
                    // Первое полное декодирование - лучший вариант
                    isBetter = true;
                    hasFullDecode = true;
                } else if (isFullDecode && hasFullDecode) {
                    // Оба полные - выбираем с большим количеством бит или лучшим качеством
                    if (candidate.bitLength > bestBits || 
                        (candidate.bitLength == bestBits && quality > bestQuality)) {
                        isBetter = true;
                    }
                } else if (!isFullDecode && !hasFullDecode) {
                    // Оба неполные - выбираем лучшее качество или больше бит
                    if (quality > bestQuality || 
                        (quality == bestQuality && candidate.bitLength > bestBits)) {
                        isBetter = true;
                    }
                }
                
                if (isBetter) {
                    bestResult = candidate;
                    bestBits = candidate.bitLength;
                    bestQuality = quality;
                }
            }
        }
    }
    
    // Дополнительные варианты для совместимости (fallback протоколы)
    // Пробуем их только если не нашли лучший результат выше
    if (!bestResult.success || bestQuality < 0.85f) {
        struct FallbackConfig {
            float highRatio;
            float lowRatio;
            bool inverted;
            int bitCount;
            const char* name;
        } fallbacks[] = {
            {1.0f, 5.0f, false, 24, "Custom 1:5"},
            {5.0f, 1.0f, false, 24, "Custom 5:1"},
        };
        
        for (const auto& cfg : fallbacks) {
            DecodedResult candidate {false, 0, 0, "", ""};
            if (decodeProtocolRCSwitch(pulses, length, estimatedTe, cfg.highRatio, cfg.lowRatio,
                                       cfg.inverted, cfg.bitCount, cfg.name, nullptr, candidate)) {
                float quality = static_cast<float>(candidate.bitLength) / cfg.bitCount;
                bool isFullDecode = (candidate.bitLength == cfg.bitCount);
                
                bool isBetter = false;
                if (isFullDecode && !hasFullDecode) {
                    isBetter = true;
                    hasFullDecode = true;
                } else if (isFullDecode && hasFullDecode && candidate.bitLength > bestBits) {
                    isBetter = true;
                } else if (!isFullDecode && !hasFullDecode && quality > bestQuality) {
                    isBetter = true;
                }
                
                if (isBetter) {
                    bestResult = candidate;
                    bestBits = candidate.bitLength;
                    bestQuality = quality;
                }
            }
        }
    }
    
    return bestResult;
}

// Вспомогательная функция для получения ожидаемого количества бит для протокола
// Использует данные из конфигурации протоколов, чтобы избежать дублирования логики
static int getExpectedBitsForProtocol(const String& protocolName, int actualBitCount) {
    // Ищем протокол в конфигурации по имени
    for (int i = 0; i < PROTOCOL_COUNT; i++) {
        const SubGhzProtocolConfig* proto = ALL_PROTOCOLS[i];
        if (!proto) break;
        
        // Сравниваем имена протоколов (с учетом вариантов типа PT2262_1:1)
        String protoName = String(proto->name);
        if (protocolName == protoName || protocolName.startsWith(protoName + "_")) {
            // Для протоколов с вариантами (CAME, Holtek, Nice FLO) проверяем фактическое количество бит
            if (protocolName == "CAME" || protocolName.startsWith("Holtek") || 
                protocolName == "Nice FLO" || protocolName == "Nice FlorS") {
                // Если фактическое количество бит меньше стандартного, возвращаем его
                if (actualBitCount > 0 && actualBitCount < proto->bitCount) {
                    return actualBitCount;
                }
            }
            return proto->bitCount;
        }
    }
    
    // Если протокол не найден, возвращаем значение по умолчанию
    return 24;
}

float CC1101Manager::findBestTE(const PulsePattern* pulses, int length, float initialTE) {
    // Собираем все короткие импульсы (вероятные TE)
    const int maxSamples = min(length, 100);
    static unsigned long samples[100];
    int sampleCount = 0;
    
    for (int i = 0; i < maxSamples && sampleCount < 100; i++) {
        unsigned long val = pulses[i].duration;
        // Берем только разумные значения
        if (val >= 100 && val <= 2000) {
            samples[sampleCount++] = val;
        }
    }
    
    if (sampleCount < 5) return initialTE;
    
    // Сортируем для поиска медианы
    std::sort(samples, samples + sampleCount);
    
    // Находим наиболее часто встречающееся значение (как в Flipper Zero)
    float bestTE = samples[sampleCount / 2]; // Медиана как начальная оценка
    int bestCount = 0;
    
    // Группируем похожие значения
    for (int i = 0; i < sampleCount; i++) {
        float testTE = static_cast<float>(samples[i]);
        int count = 0;
        
        for (int j = 0; j < sampleCount; j++) {
            float ratio = static_cast<float>(samples[j]) / testTE;
            float diff = fabsf(ratio - roundf(ratio));
            if (diff < 0.15f) { // 15% толерантность для группировки
                count++;
            }
        }
        
        if (count > bestCount) {
            bestCount = count;
            bestTE = testTE;
        }
    }
    
    return bestTE;
}

bool CC1101Manager::decodeProtocolRCSwitch(const PulsePattern* pulses, int length, float baseDelay,
                                           float highRatio, float lowRatio, bool inverted,
                                           int bitCount, const char* protocolName,
                                           const SubGhzProtocolConfig* protoConfig,
                                           DecodedResult& out) {
    // Толерантность для декодирования (35% для баланса между точностью и чувствительностью)
    const float tolerance = 0.35f;
    
    // Определяем оптимальный TE из сигнала (как в Flipper Zero)
    float optimalTE = findBestTE(pulses, length, baseDelay);
    
    // Для всех протоколов пробуем больше вариантов TE (как для CAME)
    // Это улучшает определение всех протоколов
    float teVariants[] = {
        optimalTE,
        optimalTE * 0.95f,
        optimalTE * 1.05f,
        optimalTE * 0.9f,
        optimalTE * 1.1f,
        optimalTE * 0.85f,
        optimalTE * 1.15f
    };
    // Все протоколы теперь проверяются с расширенным набором вариантов TE
    int teVariantCount = 7; // Увеличено с 3 до 7 для всех протоколов
    
    // Ограничения для TE по протоколу (более гибкие для всех протоколов)
    float minTE, maxTE;
    if (strcmp(protocolName, "CAME") == 0) {
        // CAME имеет строгий диапазон TE: 270-380 мкс (типично 320 мкс)
        minTE = 250.0f;
        maxTE = 400.0f;
    } else if (strcmp(protocolName, "Nero Radio") == 0) {
        minTE = 250.0f;  // Nero Radio обычно 300-400 мкс
        maxTE = 1000.0f; // Но может быть и больше (до 1000 мкс)
    } else if (strcmp(protocolName, "Princeton") == 0 || strcmp(protocolName, "Nero Sketch") == 0) {
        // Princeton и Nero Sketch обычно 400 мкс
        minTE = 300.0f;
        maxTE = 600.0f;
    } else if (strcmp(protocolName, "PT2262") == 0 || strcmp(protocolName, "PT2262_1:1") == 0 || 
               strcmp(protocolName, "PT2262_1:2") == 0) {
        // PT2262 обычно 400-600 мкс
        minTE = 300.0f;
        maxTE = 800.0f;
    } else if (strcmp(protocolName, "EV1527") == 0 || strcmp(protocolName, "Roger") == 0) {
        // EV1527 и Roger обычно 400-500 мкс
        minTE = 300.0f;
        maxTE = 700.0f;
    } else if (strcmp(protocolName, "Holtek") == 0) {
        // Holtek обычно 300-400 мкс
        minTE = 250.0f;
        maxTE = 500.0f;
    } else if (strcmp(protocolName, "Keeloq") == 0) {
        // Keeloq может использовать широкий диапазон TE: 400-900 мкс (в зависимости от производителя)
        // Некоторые варианты используют 650-750 мкс (как в вашем случае: 752 мкс)
        minTE = 300.0f;
        maxTE = 1000.0f; // Широкий диапазон для различных вариантов Keeloq
    } else {
        // Для остальных протоколов используем широкий диапазон, но разумный
        minTE = 150.0f;
        maxTE = 2000.0f;
    }
    
    // Максимальный пропуск преамбулы (Flipper Zero обычно пропускает до 50% для поиска начала)
    // Увеличиваем до 40% для лучшего определения всех протоколов
    int maxSkip = min(40, length / 2); // Увеличено до 40% для всех протоколов
    
    // Для всех протоколов сохраняем лучший результат (приоритет полному декодированию)
    // Это улучшает определение всех протоколов, как для CAME
    DecodedResult bestResult = {false, 0, 0, "", ""};
    int bestBits = 0;
    int bestSkip = -1;
    float bestTE = 0;
    
    for (int teIdx = 0; teIdx < teVariantCount; teIdx++) {
        float testTE = teVariants[teIdx];
        if (testTE < minTE || testTE > maxTE) continue;
        
        // Пробуем начать с разных позиций
        for (int skip = 0; skip <= maxSkip; skip++) {
            int i = skip;
            int bits = 0;
            int consecutiveMisses = 0; // Счетчик последовательных пропусков
            uint32_t testCode = 0;
            String testBitString = "";
            testBitString.reserve(bitCount + 1);
            
            // Функция проверки соответствия паттерну (как в Flipper Zero)
            auto match = [&](float a, float b, float expectedA, float expectedB) -> bool {
                float tolA = tolerance * expectedA;
                float tolB = tolerance * expectedB;
                float diffA = fabsf(a - expectedA);
                float diffB = fabsf(b - expectedB);
                return diffA <= tolA && diffB <= tolB;
            };
            
            // Декодируем биты последовательно
            // Проверяем, использует ли протокол манчестерское кодирование
            // Используем флаг из конфигурации протокола (как в Flipper Zero)
            bool useManchester = (protoConfig != nullptr) ? protoConfig->manchester : false;
            
            while (i + 1 < length && bits < bitCount) {
                float p0 = static_cast<float>(pulses[i].duration) / testTE;
                float p1 = static_cast<float>(pulses[i + 1].duration) / testTE;
                
                bool bitIdentified = false;
                bool bitValue = false;
                
                if (useManchester) {
                    // Манчестерское кодирование: каждый бит передается двумя импульсами
                    // В манчестере бит определяется по направлению перехода уровня
                    // 0: LOW->HIGH (переход от низкого к высокому)
                    // 1: HIGH->LOW (переход от высокого к низкому)
                    // Для манчестера обычно соотношение 1:1
                    float expectedRatio = (protoConfig != nullptr && protoConfig->highRatio > 0 && protoConfig->lowRatio > 0) ? 
                                         (protoConfig->highRatio / protoConfig->lowRatio) : 1.0f;
                    
                    // Проверяем манчестерские паттерны
                    // Для манчестера важны переходы уровня, а не абсолютные значения
                    if (match(p0, p1, 1.0f, 1.0f)) {
                        // Одинаковые импульсы в манчестере - определяем по уровню перехода
                        // Если текущий уровень HIGH, то переход HIGH->LOW = 1
                        // Если текущий уровень LOW, то переход LOW->HIGH = 0
                        bitValue = pulses[i].level ? 1 : 0;
                        bitIdentified = true;
                    } else if (match(p0, p1, expectedRatio, 1.0f) || match(p0, p1, 1.0f, expectedRatio)) {
                        // Разные импульсы - определяем по порядку и уровню
                        if (p0 < p1) {
                            // Короткий -> длинный: LOW->HIGH = 0
                            bitValue = 0;
                        } else {
                            // Длинный -> короткий: HIGH->LOW = 1
                            bitValue = 1;
                        }
                        bitIdentified = true;
                    }
                } else {
                    // Обычное кодирование (не манчестер)
                    // Проверяем паттерны для бита 0 и 1
                    if (!inverted) {
                        if (match(p0, p1, highRatio, lowRatio)) {
                            bitValue = 0;
                            bitIdentified = true;
                        } else if (match(p0, p1, lowRatio, highRatio)) {
                            bitValue = 1;
                            bitIdentified = true;
                        }
                    } else {
                        if (match(p0, p1, highRatio, lowRatio)) {
                            bitValue = 1;
                            bitIdentified = true;
                        } else if (match(p0, p1, lowRatio, highRatio)) {
                            bitValue = 0;
                            bitIdentified = true;
                        }
                    }
                }
                
                if (bitIdentified) {
                    testCode = (testCode << 1) | (bitValue ? 1 : 0);
                    testBitString += bitValue ? '1' : '0';
                    bits++;
                    consecutiveMisses = 0;
                    i += 2;
                } else {
                    consecutiveMisses++;
                    i++;
                    
                    // Flipper Zero строго проверяет качество декодирования
                    // Если не удалось декодировать несколько бит подряд, останавливаемся
                    // Ужесточаем: допускаем только 2 пропуска подряд для лучшей фильтрации
                    if (bits > 0 && consecutiveMisses > 2) {
                        break;
                    }
                    
                    // Если еще не начали декодировать, продолжаем поиск
                    if (bits == 0) {
                        // Но ограничиваем максимальный поиск - если не нашли начало за 20 импульсов, пропускаем
                        if (i - skip > 20) {
                            break;
                        }
                        continue;
                    }
                    
                    // Если начали декодировать, но пропустили много - это плохой сигнал
                    // Ужесточаем: допускаем только 3 пропуска если декодировано меньше половины
                    if (bits < bitCount / 2 && consecutiveMisses > 3) {
                        break;
                    }
                }
            }
            
            // Проверяем результат (как в Flipper Zero)
            // Для всех протоколов применяем универсальную логику определения качества
            // Приоритет отдаем полному декодированию, но принимаем и частичное с высоким процентом
            float minBitsRatio;
            if (bitCount >= 64) {
                // Для очень длинных протоколов (64+ бит, например Keeloq) снижаем требования до минимума
                // Keeloq может передаваться с разной длиной и вариациями, поэтому принимаем частичное декодирование
                minBitsRatio = 0.65f; // Принимаем 65% бит для Keeloq
            } else if (bitCount >= 50) {
                // Для длинных протоколов (56+ бит) снижаем требования
                minBitsRatio = 0.75f;
            } else if (bitCount >= 32) {
                // Для средних протоколов (32-49 бит)
                minBitsRatio = 0.80f;
            } else {
                // Для коротких протоколов (до 31 бит) требуем высокое качество
                // Все протоколы (включая CAME) обрабатываются одинаково
                minBitsRatio = 0.85f;
            }
            
            // Дополнительная проверка: отбрасываем подозрительные коды прямо здесь
            // Коды со всеми единицами для данного количества бит
            // Применяется ко всем протоколам одинаково
            // Для длинных протоколов (64+ бит) проверка работает только с младшими 32 битами
            if (bitCount <= 32) {
                uint32_t maxCodeForBits = (bitCount <= 24) ? 0xFFFFFF : 0xFFFFFFFF;
                if (testCode == 0 || testCode == maxCodeForBits || testCode == 0xFFFFFFFF) {
                    continue; // Пропускаем этот вариант
                }
            } else {
                // Для 64-битных протоколов проверяем только младшие 32 бита
                // Полный код может содержать любые значения, поэтому проверяем только явные ошибки
                if (testCode == 0 || testCode == 0xFFFFFFFF) {
                    // Пропускаем только явно невалидные значения
                    // Для Keeloq код может быть любым (криптографически зашифрованным)
                    continue;
                }
            }
            
            // Проверка TE применяется через ограничения minTE/maxTE выше
            // Все протоколы проверяются одинаково через общие ограничения
            
            // Flipper Zero требует высокое качество декодирования для принятия сигнала
            // Проверяем не только количество бит, но и качество соответствия паттерну
            if (bits >= bitCount * minBitsRatio && testCode != 0) {
                // Дополнительная проверка: если декодировано меньше бит, чем требуется,
                // но это неполное декодирование - требуем более высокий процент успешных бит
                // Для длинных протоколов (64+ бит) снижаем требования
                float minPartialRatio = (bitCount >= 64) ? 0.70f : 0.90f;
                if (bits < bitCount && bits < bitCount * minPartialRatio) {
                    // Для неполного декодирования требуем минимум процента успешных бит
                    continue; // Пропускаем низкокачественные варианты
                }
                // Для всех протоколов приоритет отдаем полному декодированию
                // Это улучшает определение всех протоколов, как для CAME
                bool isFullDecode = (bits == bitCount);
                bool isBetter = false;
                
                // Для всех протоколов применяем логику приоритета полного декодирования
                if (isFullDecode && bestBits < bitCount) {
                    // Если это полное декодирование и лучшего еще не было - это лучший вариант
                    isBetter = true;
                } else if (isFullDecode && bestBits == bitCount) {
                    // Если уже есть полное декодирование, но текущее тоже полное
                    // Выбираем больший код как более вероятно правильный (старшие биты)
                    isBetter = (testCode > bestResult.code);
                } else if (!isFullDecode && bestBits < bitCount && bits > bestBits) {
                    // Если лучшего полного декодирования нет, но текущее декодировало больше бит
                    isBetter = true;
                } else if (!isFullDecode && bits > bestBits) {
                    // Если оба неполные, выбираем большее количество бит
                    isBetter = true;
                }
                
                if (isBetter) {
                    bestResult.success = true;
                    bestResult.code = testCode;
                    bestResult.bitLength = bits;
                    bestResult.protocol = protocolName;
                    bestResult.bitString = testBitString;
                    bestBits = bits;
                    bestSkip = skip;
                    bestTE = testTE;
                    
                    // Для всех протоколов при полном декодировании продолжаем поиск лучшего варианта
                    // Это позволяет найти оптимальный TE и позицию начала сигнала
                    if (isFullDecode) {
                        // Не возвращаемся сразу - продолжим поиск для проверки других вариантов
                        // Это улучшает точность определения для всех протоколов
                    }
                }
            }
        }
    }
    
    // Если нашли результат, возвращаем лучший
    if (bestResult.success) {
        out = bestResult;
        
        // Логируем для всех протоколов (как для CAME)
        // Минимальный порог для логирования зависит от длины протокола
        int minBitsForLog = (bitCount >= 50) ? (int)(bitCount * 0.7f) : 
                           (bitCount >= 32) ? (int)(bitCount * 0.75f) : 
                           (int)(bitCount * 0.8f);
        
        return true;
    }
    
    return false;
}

bool CC1101Manager::decodeWithProtocols(int pulseCount, float te, uint32_t& codeOut, String& protocolName, String& bitStringOut) {
    if (pulseCount < 10) return false;
    
    // Используем статический массив вместо локального для экономии стека
    // Ограничиваем размер для обработки больших сигналов
    static PulsePattern pattern[MAX_RAW_SIGNAL_LENGTH];
    int patternLength = min(pulseCount, MAX_RAW_SIGNAL_LENGTH);
    
    // Создаем паттерн из RAW данных
    // Важно: в CC1101 в режиме OOK мы получаем переходы уровней
    // Каждый элемент массива - это длительность состояния (HIGH или LOW)
    for (int i = 0; i < patternLength; i++) {
        pattern[i].level = rawSignalLevels[i];
        pattern[i].duration = rawSignalTimings[i];
    }
    
    // Пробуем декодировать известными протоколами
    DecodedResult res = tryDecodeKnownProtocols(pattern, patternLength);
    
    if (res.success) {
        codeOut = res.code;
        protocolName = res.protocol;
        bitStringOut = res.bitString; // Битовые строки (01010101...)
        return true;
    }
    
    // Если не удалось декодировать, создаем RAW представление
    // Это позволяет сохранить данные даже без определения протокола
    String rawSequence = "";
    int maxRawItems = min(patternLength, 50); // Ограничиваем до 50 элементов для экономии памяти
    rawSequence.reserve(maxRawItems * 15); // Предварительное выделение (примерно 15 символов на элемент)
    for (int i = 0; i < maxRawItems; i++) {
        if (i > 0) rawSequence += ",";
        rawSequence += String(pattern[i].duration);
        rawSequence += pattern[i].level ? "H" : "L";
    }
    
    // Вычисляем хеш из RAW данных для идентификации
    uint32_t rawHash = computeHash(rawSignalTimings, patternLength);
    
    // Используем часть хеша как код
    codeOut = rawHash & 0xFFFFFFFF;
    protocolName = "RAW/Unknown";
    bitStringOut = rawSequence;
    
    return false; // Возвращаем false, но данные сохранены в выходных параметрах
}

bool CC1101Manager::checkReceived() {
    if (!receivedFlag || !rawSignalReady) return false;
    if (radio == nullptr) return false;

    // Фильтрация начальных сигналов
    const unsigned long INIT_FILTER_MS = 3000;
    if (initTime > 0 && (millis() - initTime) < INIT_FILTER_MS) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }

    // Получаем не-volatile копию индекса для безопасной работы
    int signalLength = static_cast<int>(rawSignalIndex);

    if (signalLength < MIN_SIGNAL_LENGTH) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }

    float estimatedTe = 0.0f;
    analyzePulsePattern(signalLength, estimatedTe); // Необязательно, Flipper-декодеры определяют TE сами
    
    CC1101* cc = (CC1101*)radio;
    int currentRssi = cc->getRSSI();
    
    // Flipper Zero использует RSSI threshold для RAW сигналов (SUBGHZ_RAW_THRESHOLD_MIN = -90 dBm)
    // Для декодированных протоколов фильтрация по RSSI не применяется
    // Но слишком слабые сигналы (< -100 dBm) часто являются шумом
    const int MIN_RSSI_FOR_VALID_SIGNAL = -100; // Минимальный RSSI для валидного сигнала
    if (currentRssi < MIN_RSSI_FOR_VALID_SIGNAL) {
        // Слишком слабый сигнал - вероятно шум
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }

    uint32_t decodedCode = 0;
    String protocolName = "RAW/Unknown";
    String bitSequence = "";
    bitSequence.reserve(200); // Предварительное выделение памяти
    
    // === Flipper Zero подход: прогоняем буфер через мультидекодер импульс за импульсом ===
    // Каждый протокол имеет свой state machine и ищет свою уникальную преамбулу.
    // Первый декодер, полностью распознавший пакет, побеждает.
    bool decoded = false;
    int decodedBitLength = 0;
    float decodedTe = 0;

    Serial.printf("[CC1101] Буфер: %d импульсов\n", signalLength);
    // (Nero Radio преамбула может быть в одном буфере, данные в следующем)
    for (int i = 0; i < signalLength; i++) {
        ::DecoderResult dr = multiDecoder.feed(rawSignalLevels[i], rawSignalTimings[i]);
        if (dr.ready) {
            decodedCode = (uint32_t)(dr.data & 0xFFFFFFFF);
            protocolName = dr.protocol;
            decodedBitLength = dr.bitCount;
            decodedTe = dr.te;
            // Формируем bitString из data
            bitSequence = "";
            for (int b = dr.bitCount - 1; b >= 0; b--) {
                bitSequence += ((dr.data >> b) & 1) ? '1' : '0';
            }
            decoded = true;
            Serial.printf("[CC1101] Flipper-декодер: %s, %d бит, код: 0x%X, TE: %.0f\n",
                          protocolName.c_str(), decodedBitLength, decodedCode, decodedTe);
            break; // Первый сработавший — победитель
        }
    }

    // Без fallback — только Flipper-декодеры
    
    // Фильтрация шумов: отбрасываем сигналы с подозрительными кодами
    if (decoded && protocolName != "RAW/Unknown") {
        // Фильтр 1: код = 0
        if (decodedCode == 0) {
            Serial.println("[CC1101] 🚫 Отфильтрован шум (код = 0)");
            resetRawBuffer();
            attachRawInterrupt();
            return false;
        }
        
        // Фильтр 2: код со всеми единицами (0xFFFFFF для 24-bit, 0xFFFFFFFF для 32-bit)
        // Это явный признак шума или ошибки декодирования
        int bitCount = bitSequence.length();
        uint32_t maxCodeForBits = (bitCount <= 24) ? 0xFFFFFF : 0xFFFFFFFF;
        if (decodedCode == maxCodeForBits || decodedCode == 0xFFFFFFFF) {
            Serial.printf("[CC1101] 🚫 Отфильтрован шум (код со всеми единицами: 0x%lX)\n", decodedCode);
            resetRawBuffer();
            attachRawInterrupt();
            return false;
        }
        
        // Фильтр 3: Универсальная проверка качества декодирования для всех протоколов
        // Применяем одинаковые критерии ко всем протоколам
        {
            // Определяем ожидаемое количество бит для протокола из конфигурации
            int expectedBits = getExpectedBitsForProtocol(protocolName, bitCount);
            
            // Проверяем качество декодирования (минимальный порог зависит от длины протокола)
            float minRatio = (expectedBits >= 50) ? 0.75f : (expectedBits >= 32) ? 0.80f : 0.85f;
            float decodeRatio = static_cast<float>(bitCount) / expectedBits;
            if (decodeRatio < minRatio) {
                Serial.printf("[CC1101] 🚫 Отфильтрован %s сигнал (слишком низкое качество: %d/%d бит, %.1f%%)\n", 
                             protocolName.c_str(), bitCount, expectedBits, decodeRatio * 100.0f);
                resetRawBuffer();
                attachRawInterrupt();
                return false;
            }
            
            // Проверка распределения бит для всех протоколов (отбрасываем подозрительно однородные коды)
            int onesCount = 0;
            for (int i = 0; i < bitCount && i < 100; i++) { // Проверяем до 100 бит для производительности
                if (bitSequence[i] == '1') onesCount++;
            }
            float onesRatio = static_cast<float>(onesCount) / min(bitCount, 100);
            // Если более 90% или менее 10% единиц - это подозрительно для любого протокола
            if (onesRatio > 0.90f || onesRatio < 0.10f) {
                Serial.printf("[CC1101] 🚫 Отфильтрован %s сигнал (подозрительное распределение бит: %.1f%% единиц)\n", 
                             protocolName.c_str(), onesRatio * 100.0f);
                resetRawBuffer();
                attachRawInterrupt();
                return false;
            }
        }
        
        // Фильтр 4: проверка битовой строки на подозрительные паттерны
        // - все единицы
        // - все нули (уже проверено выше)
        // - слишком много одинаковых бит подряд (более 80% одинаковых)
        if (bitCount > 0) {
            int onesCount = 0;
            int zerosCount = 0;
            for (int i = 0; i < bitCount; i++) {
                if (bitSequence[i] == '1') onesCount++;
                else if (bitSequence[i] == '0') zerosCount++;
            }
            
            float onesRatio = static_cast<float>(onesCount) / bitCount;
            float zerosRatio = static_cast<float>(zerosCount) / bitCount;
            
            // Если более 90% бит одинаковые - это шум
            if (onesRatio > 0.9f || zerosRatio > 0.9f) {
                Serial.printf("[CC1101] 🚫 Отфильтрован шум (подозрительный паттерн: %.1f%% единиц, %.1f%% нулей)\n", 
                             onesRatio * 100.0f, zerosRatio * 100.0f);
                resetRawBuffer();
                attachRawInterrupt();
                return false;
            }
            
            // Проверка на повторяющиеся паттерны (например, 10101010... или 11001100...)
            // Если первые 8 бит повторяются более 3 раз подряд - это шум
            if (bitCount >= 24) {
                String first8 = bitSequence.substring(0, min(8, bitCount));
                int repeatCount = 1;
                for (int i = 8; i < bitCount - 8; i += 8) {
                    String next8 = bitSequence.substring(i, min(i + 8, bitCount));
                    if (next8 == first8) {
                        repeatCount++;
                        if (repeatCount >= 3) {
                            Serial.printf("[CC1101] 🚫 Отфильтрован шум (повторяющийся паттерн: %s повторяется %d раз)\n", 
                                         first8.c_str(), repeatCount);
                            resetRawBuffer();
                            attachRawInterrupt();
                            return false;
                        }
                    } else {
                        break;
                    }
                }
            }
        }
        
        // Проверяем минимальное количество бит для декодированных протоколов
        if (bitCount < MIN_VALID_BITS) {
            Serial.printf("[CC1101] 🚫 Отфильтрован сигнал (слишком мало бит: %d)\n", bitCount);
            resetRawBuffer();
            attachRawInterrupt();
            return false;
        }
    }
    
    // Если не удалось декодировать, проверяем стоит ли сохранять RAW данные
    if (!decoded) {
        // Фильтруем слишком короткие RAW сигналы (вероятно шумы)
        // Увеличено минимальное количество переходов для RAW сигналов
        if (signalLength < MIN_RAW_SIGNAL_LENGTH) {
            // Тихо отфильтровываем - не логируем, чтобы не засорять вывод
            resetRawBuffer();
            attachRawInterrupt();
            return false;
        }
        
        // Дополнительная проверка качества для RAW сигналов
        // Проверяем стабильность TE - для хорошего сигнала отклонение должно быть небольшим
        float teStability = 0.0f;
        int stableCount = 0;
        for (int i = 0; i < signalLength; i++) {
            float ratio = static_cast<float>(rawSignalTimings[i]) / estimatedTe;
            float nearest = roundf(ratio);
            if (nearest < 0.5f) nearest = 0.5f;
            float diff = fabsf(ratio - nearest);
            if (diff < 0.4f) {
                stableCount++;
            }
        }
        
        float stabilityRatio = static_cast<float>(stableCount) / signalLength;
        // Смягчена проверка стабильности для RAW сигналов - достаточно 40% для прохождения
        if (stabilityRatio < 0.4f) {
            // Тихо отфильтровываем нестабильные сигналы
            resetRawBuffer();
            attachRawInterrupt();
            return false;
        }
        
        // Компактный вывод RAW данных для отладки
        String transitionsStr = "";
        for (int i = 0; i < min(signalLength, 10); i++) {
            if (i > 0) transitionsStr += " ";
            transitionsStr += String(rawSignalTimings[i]) + String(rawSignalLevels[i] ? 'H' : 'L');
        }
        if (signalLength > 10) transitionsStr += "...";
        
        Serial.printf("[CC1101] 🔍 RAW сигнал: переходов=%d, TE=%.1f мкс, первые переходы: %s\n", 
                      signalLength, estimatedTe, transitionsStr.c_str());
        
        // Создаем RAW представление из таймингов (ограничиваем размер)
        int maxCount = signalLength < 50 ? signalLength : 50;
        bitSequence = "";
        bitSequence.reserve(maxCount * 15);
        for (int i = 0; i < maxCount; i++) {
            if (i > 0) bitSequence += " ";
            bitSequence += String(rawSignalTimings[i]);
            bitSequence += rawSignalLevels[i] ? "H" : "L";
        }
        
        // Используем хеш как код для RAW сигнала
        decodedCode = computeHash(rawSignalTimings, signalLength) & 0xFFFFFFFF;
        
        Serial.println("[CC1101] ⚠️ Протокол не определен, сохранены RAW данные. 💡 Для отладки: пришлите эти данные вместе с данными из Flipper Zero");
    }

    uint32_t currentHash = computeHash(rawSignalTimings, signalLength);
    unsigned long now = millis();
    
    // Улучшенная проверка дубликатов: сравниваем и по хешу, и по коду+протоколу
    bool isDuplicate = false;
    
    // Проверка 1: точно такой же сигнал (хеш совпадает)
    if (lastDetectionHash == currentHash && (now - lastDetectionTime) < DUPLICATE_SUPPRESS_MS) {
        isDuplicate = true;
    }
    
    // Проверка 2: тот же код и протокол (даже если тайминги немного отличаются)
    // Используем более длинное время для декодированных протоколов
    if (!isDuplicate && decoded && protocolName != "RAW/Unknown") {
        unsigned long suppressTime = DECODED_DUPLICATE_SUPPRESS_MS;
        
        // Определяем ожидаемое количество бит для протокола из конфигурации
        int expectedBits = getExpectedBitsForProtocol(protocolName, bitSequence.length());
        
        // Проверяем, является ли это полным декодированием
        bool isFullDecode = (bitSequence.length() >= expectedBits);
        
        // Если это полностью декодированный сигнал, обновляем запись
        if (isFullDecode) {
            lastFullDecodedCode = decodedCode;
            lastFullDecodedTime = now;
        }
        
        // Проверяем дубликаты по коду и протоколу (увеличено время до 5 секунд)
        if (decodedCode == lastDetectionCode && protocolName == lastDetectionProtocol && 
            (now - lastDetectionTime) < suppressTime) {
            duplicateCount++;
            isDuplicate = true;
        }
        
        // Фильтруем частично декодированные сигналы, если недавно был полностью декодированный
        // с тем же или похожим кодом (частично декодированный код может быть частью полного)
        if (!isDuplicate && !isFullDecode && lastFullDecodedCode != 0 && 
            (now - lastFullDecodedTime) < suppressTime) {
            // Проверяем, является ли текущий код частью полного кода
            uint32_t fullCode = lastFullDecodedCode;
            uint32_t partialCode = decodedCode;
            
            // Проверяем совпадение младших 16 бит
            uint16_t fullLower16 = fullCode & 0xFFFF;
            uint16_t partialLower16 = partialCode & 0xFFFF;
            
            // Проверяем совпадение старших 16 бит (если частичный код достаточно большой)
            if (partialCode >= 0x10000) {
                uint16_t fullUpper16 = (fullCode >> 16) & 0xFFFF;
                uint16_t partialUpper16 = (partialCode >> 16) & 0xFFFF;
                
                // Если частичный код совпадает с частью полного кода, фильтруем
                if (partialLower16 == fullLower16 || partialUpper16 == fullUpper16) {
                    duplicateCount++;
                    isDuplicate = true;
                }
            } else {
                // Для маленьких кодов проверяем совпадение младших бит
                if (partialLower16 == fullLower16) {
                    duplicateCount++;
                    isDuplicate = true;
                }
            }
        }
    }
    
    // Проверка 3: для RAW сигналов - сравниваем по хешу с небольшой толерантностью
    if (!isDuplicate && !decoded && protocolName == "RAW/Unknown" && lastDetectionProtocol == "RAW/Unknown") {
        // Для RAW сигналов считаем дубликатом если хеш совпадает или очень похож
        if ((now - lastDetectionTime) < DUPLICATE_SUPPRESS_MS && lastDetectionHash != 0) {
            uint32_t hashDiff = (currentHash > lastDetectionHash) ? 
                                (currentHash - lastDetectionHash) : 
                                (lastDetectionHash - currentHash);
            // Если хеши очень похожи (разница < 1% от хеша или < 1000), считаем дубликатом
            uint32_t tolerance = (lastDetectionHash > 100000) ? (lastDetectionHash / 100) : 1000;
            if (hashDiff < tolerance || currentHash == lastDetectionHash) {
                duplicateCount++;
                isDuplicate = true;
            }
        }
    }
    
    if (isDuplicate) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }
    
    // Новый уникальный сигнал - сохраняем данные
    int skippedDuplicates = duplicateCount; // Сохраняем количество пропущенных повторений
    lastDetectionHash = currentHash;
    lastDetectionTime = now;
    lastDetectionCode = decodedCode;
    lastDetectionProtocol = protocolName;
    duplicateCount = 0; // Сбрасываем счетчик

    // Компактный вывод информации об обнаруженном сигнале
    String signalInfo = "";
    if (skippedDuplicates > 0) {
        signalInfo = String(" | Пропущено повторов: ") + String(skippedDuplicates);
    }
    Serial.printf("[CC1101] 📡 Сигнал: переходов=%d, TE=%.1f мкс%s\n", signalLength, estimatedTe, signalInfo.c_str());
    
    // Универсальная обработка для всех протоколов
    // Специальные проверки для конкретных ключей могут быть добавлены здесь, но они не влияют на определение протокола

    lastKey.available = true;
    lastKey.timestamp = millis();
    lastKey.dataLength = signalLength;
    lastKey.rssi = currentRssi;
    lastKey.snr = 0.0;
    lastKey.frequencyError = 0.0;
    lastKey.code = decodedCode;
    lastKey.rawData = bitSequence;
    lastKey.protocol = protocolName;
    lastKey.modulation = "ASK/OOK";
    lastKey.hash = currentHash;
    
    // Извлекаем битовую строку из decoded результата
    // Для декодированных протоколов bitSequence содержит битовую строку (0101...)
    // Для RAW протоколов содержит тайминги (939L 869H ...)
    if (decoded && protocolName != "RAW/Unknown") {
        // bitSequence уже содержит битовую строку из decodeWithProtocols
        // Проверяем, что это действительно битовая строка (только 0 и 1)
        bool isBitString = true;
        int bitCount = 0;
        for (int i = 0; i < bitSequence.length(); i++) {
            char c = bitSequence[i];
            if (c == '0' || c == '1') {
                bitCount++;
            } else if (c != ' ' && c != '\n' && c != '\r') {
                // Если есть другие символы (например, L, H для RAW) - это не битовая строка
                isBitString = false;
                break;
            }
        }
        
        if (isBitString && bitCount > 0) {
            // Это битовая строка - сохраняем без пробелов
            lastKey.bitString = "";
            lastKey.bitString.reserve(bitCount);
            for (int i = 0; i < bitSequence.length(); i++) {
                char c = bitSequence[i];
                if (c == '0' || c == '1') {
                    lastKey.bitString += c;
                }
            }
            lastKey.bitLength = bitCount;
        } else {
            // Это RAW данные, битовая строка недоступна
            lastKey.bitString = "";
            lastKey.bitLength = 0;
        }
    } else {
        // Для RAW сигналов битовая строка пустая
        lastKey.bitString = "";
        lastKey.bitLength = 0;
    }
    lastKey.te = (decodedTe > 0) ? decodedTe : estimatedTe;
    
    // Компактный однострочный вывод информации о ключе
    String hexCode = String(lastKey.code, HEX);
    hexCode.toUpperCase();
    
    // Для длинных протоколов (56+ бит) показываем полный код из bitString
    String fullHexCode = hexCode;
    if (bitSequence.length() >= 50) {
        // Для длинных кодов показываем младшие 32 бита в hex
        if (bitSequence.length() >= 56) {
            fullHexCode = String(lastKey.code, HEX);
            fullHexCode.toUpperCase();
            // Добавляем индикатор, что это часть длинного кода
            fullHexCode = "..." + fullHexCode;
        }
    }
    
    // Обрезаем длинную последовательность для вывода
    String displayData = bitSequence;
    if (displayData.length() > 30) {
        displayData = displayData.substring(0, 27) + "...";
    }
    
    // Для длинных протоколов показываем дополнительную информацию
    if (protocolName == "Keeloq" || protocolName == "Keeloq64") {
        // Keeloq - 64-битный протокол
        String bitDisplay = bitSequence.length() > 70 ? (bitSequence.substring(0, 70) + "...") : bitSequence;
        Serial.printf("[CC1101] 🔑 Ключ: %s (64-bit) | Код: %lu (0x%s) | Битовая строка: %s | RSSI: %d dBm | TE: %.0f мкс | Частота: %.2f МГц\n",
                      lastKey.protocol.c_str(), lastKey.code, hexCode.c_str(), 
                      bitDisplay.c_str(),
                      lastKey.rssi, estimatedTe, currentFrequency);
    } else if (bitSequence.length() >= 50) {
        // Другие длинные протоколы (56-bit и т.д.)
        Serial.printf("[CC1101] 🔑 Ключ: %s (%d-bit) | Код: %lu (0x%s) | Битовая строка: %s | RSSI: %d dBm | TE: %.0f мкс | Частота: %.2f МГц\n",
                      lastKey.protocol.c_str(), bitSequence.length(), lastKey.code, hexCode.c_str(), 
                      bitSequence.length() > 60 ? (bitSequence.substring(0, 60) + "...").c_str() : bitSequence.c_str(),
                      lastKey.rssi, estimatedTe, currentFrequency);
    } else {
        Serial.printf("[CC1101] 🔑 Ключ: %s | Код: %lu (0x%s) | RSSI: %d dBm | TE: %.0f мкс | Частота: %.2f МГц | Переходов: %d | Данные: %s\n",
                      lastKey.protocol.c_str(), lastKey.code, hexCode.c_str(), lastKey.rssi, 
                      estimatedTe, currentFrequency, signalLength, displayData.c_str());
    }
        
    resetRawBuffer();
    attachRawInterrupt();
        return true;
}

// Получить принятый ключ
ReceivedKey CC1101Manager::getReceivedKey() {
    return lastKey;
}

// Сброс принятых данных
void CC1101Manager::resetReceived() {
    lastKey.available = false;
    lastKey.code = 0;
    lastKey.rawData = "";
    lastKey.bitString = "";
    lastKey.bitLength = 0;
    lastKey.te = 0.0f;
    lastKey.hash = 0;
    receivedFlag = false;
    rawSignalReady = false;
    rawSignalIndex = 0;
    lastInterruptTime = 0;
    Serial.println("[CC1101] Буфер приема очищен");
}

// Получить RSSI
int CC1101Manager::getRSSI() {
    if (radio == nullptr) return -999;
    CC1101* cc = (CC1101*)radio;
    return cc->getRSSI();
}

// Установить битрейт
bool CC1101Manager::setBitRate(float br) {
    if (radio == nullptr) return false;
    CC1101* cc = (CC1101*)radio;
    int state = cc->setBitRate(br);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] Битрейт установлен: ");
        Serial.print(br);
        Serial.println(" kbps");
        return true;
    }
    return false;
}

// Установить девиацию частоты
bool CC1101Manager::setFrequencyDeviation(float freqDev) {
    if (radio == nullptr) return false;
    CC1101* cc = (CC1101*)radio;
    int state = cc->setFrequencyDeviation(freqDev);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] Девиация частоты установлена: ");
        Serial.print(freqDev);
        Serial.println(" кГц");
        return true;
    }
    return false;
}

// Установить ширину полосы приемника
bool CC1101Manager::setRxBandwidth(float rxBw) {
    if (radio == nullptr) return false;
    CC1101* cc = (CC1101*)radio;
    int state = cc->setRxBandwidth(rxBw);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] Ширина полосы RX установлена: ");
        Serial.print(rxBw);
        Serial.println(" кГц");
        return true;
    }
    return false;
}

// Вывод информации о конфигурации
void CC1101Manager::printConfig() {
    Serial.println("\n╔════════════════════════════════════════════════════════════╗");
    Serial.println("║           КОНФИГУРАЦИЯ CC1101                              ║");
    Serial.println("╠════════════════════════════════════════════════════════════╣");
    Serial.printf("║ Частота:          %-33.2f МГц║\n", currentFrequency);
    Serial.println("║ Модуляция:        AM650 (ASK/OOK)                         ║");
    Serial.println("║ Битрейт:          3.79 kbps                                ║");
    Serial.println("║ Ширина полосы RX: 58.0 кГц                                 ║");
    Serial.println("║ Девиация:         5.2 кГц                                  ║");
    Serial.println("║ Выходная мощность:10 dBm                                   ║");
    Serial.println("║ Режим:            RAW OOK (Direct Mode)                    ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");
}

// Получить счетчик прерываний (для отладки)
unsigned long CC1101Manager::getInterruptCount() {
    return interruptCounter;
}

bool CC1101Manager::init(int csPin, int gdo0Pin, int gdo2Pin) {
    Serial.println("[CC1101] Инициализация модуля...");
    Serial.println("[CC1101] CS: GPIO" + String(csPin) + ", GDO0: GPIO" + String(gdo0Pin) + ", GDO2: GPIO" + String(gdo2Pin));

    gdo0PinNumber = gdo0Pin;

    Module* mod = new Module(csPin, gdo0Pin, RADIOLIB_NC, gdo2Pin);
    radio = new CC1101(mod);
    CC1101* cc = static_cast<CC1101*>(radio);

    Serial.print("[CC1101] Настройка на частоту ");
    Serial.print(currentFrequency);
    Serial.println(" МГц...");

    int state = cc->begin(currentFrequency);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] ❌ Ошибка инициализации, код: ");
        Serial.println(state);
        return false;
    }
    Serial.println("[CC1101] ✅ Инициализация успешна!");

    // ============================================
    // FIXED SCAN MODE (как в Flipper Zero)
    // ============================================
    // Режим фиксированного сканирования:
    // - Фиксированная частота: 433.92 МГц (установлена выше)
    // - Фиксированная модуляция: AM650 (ASK/OOK)
    // - Автоматическое определение протокола из RAW данных
    // - Проверка всех известных протоколов в порядке приоритета
    // - Автоматическое определение TE из сигнала
    // ============================================
    
    // Настройка модуляции AM650 (как в Flipper Zero)
    state = cc->setOOK(true);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ⚠️ Ошибка установки OOK модуляции");
    }

    // RadioLib API — проверенная рабочая конфигурация
    // При BW=58kHz данные имеют правильную OOK структуру:
    // 340H 1667L 230H 753L — чередование разных длительностей
    state = cc->setBitRate(3.79);
    Serial.println("[CC1101] Битрейт: 3.79 kbps");

    state = cc->setRxBandwidth(58.0);
    Serial.println("[CC1101] BW: 58 кГц");

    state = cc->setFrequencyDeviation(5.2);
    Serial.println("[CC1101] Девиация: 5.2 кГц");

    state = cc->setOutputPower(10);
    Serial.println("[CC1101] Мощность: 10 dBm");

    if (!configureForRawMode()) {
        Serial.println("[CC1101] ❌ Не удалось настроить RAW режим");
        return false;
    }

    printConfig();
    initTime = millis();
    return enterRawReceive();
}

bool CC1101Manager::setFrequency(float freq) {
    if (radio == nullptr) return false;
    Serial.print("[CC1101] Изменение частоты на ");
    Serial.print(freq);
    Serial.println(" МГц...");

    CC1101* cc = static_cast<CC1101*>(radio);
    int state = cc->setFrequency(freq);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[CC1101] ❌ Ошибка изменения частоты, код: ");
        Serial.println(state);
        return false;
    }

    currentFrequency = freq;
    Serial.print("[CC1101] ✅ Частота изменена на ");
    Serial.print(freq);
    Serial.println(" МГц");
    return enterRawReceive();
}

float CC1101Manager::getFrequency() {
    return currentFrequency;
}

// Установка модуляции (для будущего использования)
// Пока реализована только базовая структура, полная реализация будет добавлена позже
bool CC1101Manager::setModulation(ModulationType mod) {
    if (!radio) return false;
    
    CC1101* cc = static_cast<CC1101*>(radio);
    int state = RADIOLIB_ERR_NONE;
    
    // Сохраняем состояние модуля перед изменением
    state = cc->standby();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ⚠️ Ошибка перевода в standby для смены модуляции");
        return false;
    }
    
    // Устанавливаем модуляцию в зависимости от типа
    switch (mod) {
        case MODULATION_ASK_OOK:
            state = cc->setOOK(true);
            if (state == RADIOLIB_ERR_NONE) {
                currentModulation = MODULATION_ASK_OOK;
                Serial.println("[CC1101] Модуляция установлена: ASK/OOK");
            }
            break;
            
        case MODULATION_FSK_2FSK:
            state = cc->setOOK(false); // Отключаем OOK для FSK
            // Для FSK нужны дополнительные настройки (битрейт, девиация)
            // Пока базовая структура, полная реализация будет добавлена позже
            if (state == RADIOLIB_ERR_NONE) {
                currentModulation = MODULATION_FSK_2FSK;
                Serial.println("[CC1101] Модуляция установлена: FSK/2-FSK (базовая)");
            }
            break;
            
        case MODULATION_MSK:
        case MODULATION_GFSK:
            // MSK и GFSK требуют специальных настроек
            // Пока базовая структура, полная реализация будет добавлена позже
            Serial.println("[CC1101] ⚠️ MSK/GFSK модуляция пока не реализована полностью");
            return false;
            
        default:
            Serial.println("[CC1101] ⚠️ Неизвестный тип модуляции");
            return false;
    }
    
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[CC1101] ⚠️ Ошибка установки модуляции, код: %d\n", state);
        return false;
    }
    
    return true;
}

// Получение текущей модуляции
ModulationType CC1101Manager::getModulation() {
    return currentModulation;
}

bool CC1101Manager::startReceive() {
    return enterRawReceive();
}

void IRAM_ATTR CC1101Manager::onInterrupt() {
    unsigned long now = micros();
    bool level = digitalRead(gdo0PinNumber);

    if (!firstEdgeCaptured) {
        lastInterruptTime = now;
        lastSignalLevel = level;
        firstEdgeCaptured = true;
        interruptCounter = 0;
        return;
    }

    unsigned long delta = now - lastInterruptTime;
    lastInterruptTime = now;
    interruptCounter++;

    // Склеиваем очень короткие импульсы (шум)
    if (delta < GLUE_THRESHOLD_US) {
        if (rawSignalIndex > 0) {
            rawSignalTimings[rawSignalIndex - 1] += delta;
        }
        lastSignalLevel = level;
        return;
    }

    // Слишком короткие импульсы — шум, пропускаем
    if (delta < MIN_PULSE_US) {
        lastSignalLevel = level;
        return;
    }

    // Длинная пауза: может быть преамбула (CAME=18000, Nice FLO=25200) или конец сигнала
    if (delta > MAX_PULSE_US) {
        if (rawSignalIndex >= MIN_PULSES_TO_ACCEPT) {
            rawSignalReady = true;
            receivedFlag = true;
            detachRawInterrupt();
        } else {
            // Мало данных — сброс
            rawSignalIndex = 0;
            firstEdgeCaptured = false;
        }
        lastSignalLevel = level;
        return;
    }

    // Линейный буфер
    if (rawSignalIndex < MAX_RAW_SIGNAL_LENGTH - 1) {
        rawSignalTimings[rawSignalIndex] = delta;
        rawSignalLevels[rawSignalIndex] = lastSignalLevel;
        rawSignalIndex++;
    }

    // Конец сигнала: gap > END_GAP или буфер полный
    if (delta > END_GAP_US || rawSignalIndex >= MAX_RAW_SIGNAL_LENGTH - 1) {
        if (rawSignalIndex >= MIN_PULSES_TO_ACCEPT) {
            rawSignalReady = true;
            receivedFlag = true;
            detachRawInterrupt();
        } else {
            rawSignalIndex = 0;
            firstEdgeCaptured = false;
        }
    }

    lastSignalLevel = level;
}

