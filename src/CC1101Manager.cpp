#include "CC1101Manager.h"
#include "SubGhzProtocols.h"
#include <math.h>
#include <algorithm>

namespace {
    constexpr unsigned long MIN_PULSE_US = 200;
    constexpr unsigned long MAX_PULSE_US = 15000;
    constexpr unsigned long END_GAP_US = 5000;
    constexpr unsigned long GLUE_THRESHOLD_US = 40;
    constexpr int MIN_PULSES_TO_ACCEPT = 40;
    constexpr unsigned long DUPLICATE_SUPPRESS_MS = 3000; // Для RAW сигналов
    constexpr unsigned long DECODED_DUPLICATE_SUPPRESS_MS = 5000; // Для декодированных протоколов (5 секунд)
    // RSSI threshold отключен - как во Flipper Zero, фильтрация по RSSI не используется
    constexpr float TE_VARIANCE_LIMIT = 0.25f;
    constexpr int MIN_VALID_BITS = 12; // Минимум бит для валидного протокола (отфильтровываем код 0)
    constexpr int MIN_SIGNAL_LENGTH = 30; // Минимум переходов для валидного сигнала (баланс между фильтрацией и чувствительностью)
    constexpr int MIN_RAW_SIGNAL_LENGTH = 40; // Минимум переходов для RAW сигнала
    constexpr float MIN_PATTERN_CONFIDENCE = 0.5f; // Минимум уверенности в наличии паттерна (50% импульсов должны группироваться)
}

// Статические переменные
CC1101* CC1101Manager::radio = nullptr;
float CC1101Manager::currentFrequency = 434.42;
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
    
    // Устанавливаем GDO0 -> RAW данные, GDO2 -> тактовый сигнал (как на Flipper)
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
    // Более строгая проверка качества сигнала
    if (pulseCount < MIN_SIGNAL_LENGTH) {
        return false;
    }
    
    long sum = 0;
    unsigned long maxPulse = 0;
    unsigned long minPulse = UINT32_MAX;
    int validPulses = 0;
    
    for (int i = 0; i < pulseCount; i++) {
        unsigned long val = rawSignalTimings[i];
        if (val >= MIN_PULSE_US && val <= MAX_PULSE_US) {
            if (val > maxPulse) maxPulse = val;
            if (val < minPulse) minPulse = val;
            sum += val;
            validPulses++;
        }
    }
    
    // Должно быть достаточно валидных импульсов (75% для лучшей фильтрации шумов)
    if (validPulses < pulseCount * 0.75f) {
        return false; // Слишком много невалидных импульсов
    }
    
    if (validPulses == 0) return false;
    
    float average = static_cast<float>(sum) / validPulses;
    if (average < MIN_PULSE_US || average > MAX_PULSE_US) {
        return false;
    }
    
    // Более строгая проверка разброса значений (ужесточено до 3.5x для лучшей фильтрации)
    if (maxPulse > average * 3.5f || minPulse < average / 3.5f) {
        return false;
    }
    
    // Проверка на наличие паттерна: импульсы должны группироваться вокруг нескольких значений
    // Это признак структурированного сигнала, а не случайного шума
    const int patternGroups = 5; // Количество групп для анализа паттерна
    int groupCounts[patternGroups] = {0};
    float groupSize = (maxPulse - minPulse) / patternGroups;
    if (groupSize < 50) groupSize = 50; // Минимальный размер группы
    
    for (int i = 0; i < pulseCount; i++) {
        unsigned long val = rawSignalTimings[i];
        if (val >= MIN_PULSE_US && val <= MAX_PULSE_US) {
            int group = (int)((val - minPulse) / groupSize);
            if (group >= 0 && group < patternGroups) {
                groupCounts[group]++;
            }
        }
    }
    
    // Проверяем, что импульсы не распределены равномерно (это было бы шумом)
    // Хороший сигнал имеет несколько доминирующих групп
    int maxGroupCount = 0;
    int totalGrouped = 0;
    for (int i = 0; i < patternGroups; i++) {
        if (groupCounts[i] > maxGroupCount) {
            maxGroupCount = groupCounts[i];
        }
        totalGrouped += groupCounts[i];
    }
    
    // Если самая большая группа содержит менее 30% импульсов, это вероятно шум
    // Ужесточено для лучшей фильтрации ложных срабатываний
    if (totalGrouped > 0 && maxGroupCount < totalGrouped * 0.30f) {
        return false;
    }
    
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
    DecodedResult result {false, 0, 0, "", ""};
    
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
    
    // Используем протоколы из SubGhzProtocols (адаптированные из Flipper Zero)
    // Пробуем каждый протокол с разными вариантами кодирования
    
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
            
            if (decodeProtocolRCSwitch(pulses, length, baseDelay, variant.highRatio, variant.lowRatio,
                                       variant.inverted, proto->bitCount, proto->name, result)) {
                return result;
            }
        }
    }
    
    // Дополнительные варианты для совместимости (старая логика)
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
        if (decodeProtocolRCSwitch(pulses, length, estimatedTe, cfg.highRatio, cfg.lowRatio,
                                   cfg.inverted, cfg.bitCount, cfg.name, result)) {
            return result;
        }
    }
    
    return result;
}

// Вспомогательная функция для определения TE из сигнала (как в Flipper Zero)
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
                                           DecodedResult& out) {
    // Толерантность для декодирования (35% для баланса между точностью и чувствительностью)
    const float tolerance = 0.35f;
    
    // Определяем оптимальный TE из сигнала (как в Flipper Zero)
    float optimalTE = findBestTE(pulses, length, baseDelay);
    
    // Для CAME и Nero Radio пробуем больше вариантов TE
    float teVariants[] = {
        optimalTE,
        optimalTE * 0.95f,
        optimalTE * 1.05f,
        optimalTE * 0.9f,
        optimalTE * 1.1f
    };
    bool isCameOrNero = (strcmp(protocolName, "CAME") == 0) || (strcmp(protocolName, "Nero Radio") == 0);
    int teVariantCount = isCameOrNero ? 5 : 3;
    
    // Ограничения для TE по протоколу
    float minTE, maxTE;
    if (strcmp(protocolName, "CAME") == 0) {
        // CAME имеет строгий диапазон TE: 270-380 мкс (типично 320 мкс)
        // Ужесточаем проверку для предотвращения ложных срабатываний
        minTE = 250.0f;
        maxTE = 400.0f;
    } else if (strcmp(protocolName, "Nero Radio") == 0) {
        minTE = 250.0f;  // Nero Radio обычно 300-400 мкс
        maxTE = 1000.0f; // Но может быть и больше (до 1000 мкс)
    } else {
        minTE = 100.0f;
        maxTE = 2000.0f;
    }
    
    // Максимальный пропуск преамбулы (Flipper Zero обычно пропускает до 50% для поиска начала)
    // Но для предотвращения ложных срабатываний ограничиваем до 30% для большинства протоколов
    int maxSkip = min(30, length / 3); // Ограничиваем до 30% вместо 50%
    
    // Для CAME сохраняем лучший результат (приоритет полному декодированию)
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
            // (определяется из конфигурации протокола, но пока проверяем по имени)
            bool useManchester = false; // TODO: получить из proto->manchester
            // Временная проверка по имени протокола (можно будет убрать после добавления флага)
            // В данной реализации пока не используется, но структура готова
            
            while (i + 1 < length && bits < bitCount) {
                float p0 = static_cast<float>(pulses[i].duration) / testTE;
                float p1 = static_cast<float>(pulses[i + 1].duration) / testTE;
                
                bool bitIdentified = false;
                bool bitValue = false;
                
                if (useManchester) {
                    // Манчестерское кодирование: каждый бит передается двумя импульсами
                    // 0: LOW->HIGH (короткий LOW, длинный HIGH)
                    // 1: HIGH->LOW (длинный HIGH, короткий LOW)
                    // Обычно соотношение 1:1 или 1:2
                    // Для упрощения пробуем оба варианта
                    if (match(p0, p1, 1.0f, 1.0f)) {
                        // Одинаковые импульсы - возможно 0 или 1 в зависимости от порядка
                        // В манчестере важно учитывать порядок переходов
                        bitValue = (p0 < p1) ? 0 : 1; // Упрощенная логика
                        bitIdentified = true;
                    } else if (match(p0, p1, 1.0f, 2.0f) || match(p0, p1, 2.0f, 1.0f)) {
                        // Разные импульсы - бит определяется по порядку
                        bitValue = (p0 < p1) ? 0 : 1;
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
            // Для CAME требуем строгое декодирование: минимум 95% или полное
            // Для других протоколов: >= 80% бит для валидного результата
            // Для длинных протоколов (56 бит) требуем минимум 75% для лучшей чувствительности
            float minBitsRatio;
            if (strcmp(protocolName, "CAME") == 0) {
                // CAME требует высокое качество декодирования для предотвращения ложных срабатываний
                minBitsRatio = 0.95f; // 95% или полное декодирование
            } else if (bitCount >= 50) {
                minBitsRatio = 0.75f; // Для длинных протоколов
            } else {
                minBitsRatio = 0.8f;  // Стандартный порог
            }
            
            // Дополнительная проверка: отбрасываем подозрительные коды прямо здесь
            // Коды со всеми единицами для данного количества бит
            uint32_t maxCodeForBits = (bitCount <= 24) ? 0xFFFFFF : 0xFFFFFFFF;
            if (testCode == 0 || testCode == maxCodeForBits || testCode == 0xFFFFFFFF) {
                continue; // Пропускаем этот вариант
            }
            
            // Для CAME дополнительно проверяем TE - должен быть в правильном диапазоне
            if (strcmp(protocolName, "CAME") == 0) {
                // TE для CAME должен быть 270-380 мкс, проверяем строго
                // Допускаем небольшое отклонение (±10%) для учета вариаций в реальных сигналах
                if (testTE < 240.0f || testTE > 420.0f) {
                    continue; // Пропускаем варианты с неправильным TE
                }
            }
            
            // Flipper Zero требует высокое качество декодирования для принятия сигнала
            // Проверяем не только количество бит, но и качество соответствия паттерну
            if (bits >= bitCount * minBitsRatio && testCode != 0) {
                // Дополнительная проверка: если декодировано меньше бит, чем требуется,
                // но это неполное декодирование - требуем более высокий процент успешных бит
                if (bits < bitCount && bits < bitCount * 0.9f) {
                    // Для неполного декодирования требуем минимум 90% успешных бит
                    continue; // Пропускаем низкокачественные варианты
                }
                // Для CAME 24-bit и Nero Radio 56-bit приоритет отдаем полному декодированию
                bool isFullDecode = (bits == bitCount);
                bool isBetter = false;
                
                if ((strcmp(protocolName, "CAME") == 0 && bitCount == 24) ||
                    (strcmp(protocolName, "Nero Radio") == 0 && bitCount == 56)) {
                    // Если это полное декодирование и лучшего еще не было - это лучший вариант
                    if (isFullDecode && bestBits < bitCount) {
                        isBetter = true;
                    }
                    // Если уже есть полное декодирование, но текущее тоже полное
                    // Для Nero Radio выбираем большее количество декодированных бит
                    else if (isFullDecode && bestBits == bitCount) {
                        // Выбираем больший код как более вероятно правильный (старшие биты)
                        isBetter = (testCode > bestResult.code);
                    }
                    // Если лучшего полного декодирования нет, но текущее декодировало больше бит
                    else if (!isFullDecode && bestBits < bitCount && bits > bestBits) {
                        isBetter = true;
                    }
                    // Если оба неполные, выбираем большее количество бит
                    else if (!isFullDecode && bits > bestBits) {
                        isBetter = true;
                    }
                } else {
                    // Для других протоколов выбираем большее количество декодированных бит
                    if (bits > bestBits) {
                        isBetter = true;
                    }
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
                    
                    // Если нашли полное декодирование для CAME или Nero Radio, продолжим поиск лучшего
                    if (((strcmp(protocolName, "CAME") == 0 && bitCount == 24) ||
                         (strcmp(protocolName, "Nero Radio") == 0 && bitCount == 56)) && isFullDecode) {
                        // Не возвращаемся сразу - продолжим поиск для проверки других вариантов
                    }
                }
            }
        }
    }
    
    // Если нашли результат, возвращаем лучший
    if (bestResult.success) {
        out = bestResult;
        
        // Логируем для CAME и Nero Radio
        if (strcmp(protocolName, "CAME") == 0 && bestResult.bitLength >= 20) {
            Serial.printf("[CAME] Декодировано: skip=%d, TE=%.1f, bits=%d/%d, code=%lu (0x%lX)\n", 
                         bestSkip, bestTE, bestResult.bitLength, bitCount, bestResult.code, bestResult.code);
        } else if (strcmp(protocolName, "Nero Radio") == 0 && bestResult.bitLength >= 40) {
            Serial.printf("[Nero Radio] Декодировано: skip=%d, TE=%.1f, bits=%d/%d, code=%lu (0x%lX)\n", 
                         bestSkip, bestTE, bestResult.bitLength, bitCount, bestResult.code, bestResult.code);
        }
        
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
        bitStringOut = res.bitString;
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

    // Фильтрация начальных сигналов: игнорируем сигналы в первые 3 секунды после инициализации
    const unsigned long INIT_FILTER_MS = 3000;
    if (initTime > 0 && (millis() - initTime) < INIT_FILTER_MS) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }

    // Получаем не-volatile копию индекса для безопасной работы
    int signalLength = static_cast<int>(rawSignalIndex);

    if (!signalLooksValid(signalLength)) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }

    float estimatedTe = 0.0f;
    if (!analyzePulsePattern(signalLength, estimatedTe)) {
        resetRawBuffer();
        attachRawInterrupt();
        return false;
    }
    
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
    
    // Пробуем декодировать протокол (signalLength уже определена выше)
    bool decoded = decodeWithProtocols(signalLength, estimatedTe, decodedCode, protocolName, bitSequence);
    
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
        
        // Фильтр 3: Для CAME - строгая проверка качества декодирования и TE
        if (protocolName == "CAME") {
            // CAME должен быть декодирован минимум на 95% (23/24 или 24/24 бита)
            float decodeRatio = static_cast<float>(bitCount) / 24.0f;
            if (decodeRatio < 0.95f) {
                Serial.printf("[CC1101] 🚫 Отфильтрован CAME сигнал (слишком низкое качество: %d/24 бит, %.1f%%)\n", 
                             bitCount, decodeRatio * 100.0f);
                resetRawBuffer();
                attachRawInterrupt();
                return false;
            }
            
            // CAME TE должен быть в диапазоне 270-380 мкс (строгая проверка)
            if (estimatedTe < 250.0f || estimatedTe > 400.0f) {
                Serial.printf("[CC1101] 🚫 Отфильтрован CAME сигнал (неправильный TE: %.1f мкс, ожидается 270-380 мкс)\n", estimatedTe);
                resetRawBuffer();
                attachRawInterrupt();
                return false;
            }
            
            // Дополнительная проверка: CAME обычно имеет определенную структуру
            // Проверяем, что битовая строка не слишком однородна (не все единицы/нули)
            int onesCount = 0;
            for (int i = 0; i < bitCount; i++) {
                if (bitSequence[i] == '1') onesCount++;
            }
            float onesRatio = static_cast<float>(onesCount) / bitCount;
            // CAME коды обычно имеют баланс между единицами и нулями
            // Если более 85% или менее 15% единиц - это подозрительно
            if (onesRatio > 0.85f || onesRatio < 0.15f) {
                Serial.printf("[CC1101] 🚫 Отфильтрован CAME сигнал (подозрительное распределение бит: %.1f%% единиц)\n", 
                             onesRatio * 100.0f);
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
        
        // Определяем ожидаемое количество бит для протокола
        int expectedBits = 24; // По умолчанию для CAME
        if (protocolName == "CAME") {
            expectedBits = 24;
        } else if (protocolName == "Princeton" || protocolName == "Gate TX") {
            expectedBits = 24;
        } else if (protocolName == "EV1527" || protocolName == "Roger") {
            expectedBits = 28;
        }
        
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
    
    // Специальная проверка для CAME протокола - сравнение с ожидаемым ключом из Flipper Zero
    // Ожидаемый ключ: 00 00 00 00 00 FD 85 2B (последние 3 байта: FD 85 2B)
    // В формате 24-bit: 0xFD852B = 16611243 (big-endian) или 0x2B85FD = 2850301 (little-endian)
    if (protocolName == "CAME" && decodedCode != 0) {
        uint32_t expectedCodeBE = 0xFD852B;  // Big-endian: FD 85 2B
        uint32_t expectedCodeLE = 0x2B85FD;  // Little-endian: 2B 85 FD
        
        bool matchesBE = (decodedCode == expectedCodeBE) || 
                         (decodedCode > expectedCodeBE * 0.99 && decodedCode < expectedCodeBE * 1.01);
        bool matchesLE = (decodedCode == expectedCodeLE) || 
                         (decodedCode > expectedCodeLE * 0.99 && decodedCode < expectedCodeLE * 1.01);
        
        if (matchesBE || matchesLE) {
            Serial.printf("[CAME] ✅ КОД СООТВЕТСТВУЕТ КЛЮЧУ! Декодировано: %lu (0x%lX), ожидалось: %lu (0x%lX) или %lu (0x%lX)\n",
                         decodedCode, decodedCode, expectedCodeBE, expectedCodeBE, expectedCodeLE, expectedCodeLE);
        } else {
            uint32_t lower12Bits = decodedCode & 0xFFF;
            uint32_t upper12Bits = (decodedCode >> 12) & 0xFFF;
            uint32_t expectedLower12 = expectedCodeBE & 0xFFF;
            uint32_t expectedUpper12 = (expectedCodeBE >> 12) & 0xFFF;
            
            if (lower12Bits == expectedLower12 || upper12Bits == expectedUpper12) {
                Serial.printf("[CAME] ⚠️ Декодирована часть ключа: %lu (0x%lX), ожидалось: %lu (0x%lX)\n",
                             decodedCode, decodedCode, expectedCodeBE, expectedCodeBE);
            } else {
                Serial.printf("[CAME] 🔍 Декодировано: %lu (0x%lX), ожидалось: %lu (0x%lX) или %lu (0x%lX)\n",
                             decodedCode, decodedCode, expectedCodeBE, expectedCodeBE, expectedCodeLE, expectedCodeLE);
            }
        }
    }

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
    
    // Компактный однострочный вывод информации о ключе
    String hexCode = String(lastKey.code, HEX);
    hexCode.toUpperCase();
    
    // Для длинных протоколов (56 бит) показываем полный код из bitString
    String fullHexCode = hexCode;
    if (protocolName == "Nero Radio" && bitSequence.length() >= 50) {
        // Для 56-битных кодов показываем последние 8 символов (младшие 32 бита)
        // и первые символы из полного кода
        if (bitSequence.length() >= 56) {
            // Пытаемся извлечь hex из битовой строки
            // Но для простоты показываем младшие 32 бита в hex
            fullHexCode = String(lastKey.code, HEX);
            fullHexCode.toUpperCase();
            // Добавляем индикатор, что это часть 56-битного кода
            fullHexCode = "...XXXX" + fullHexCode; // XXXX будет заменено на старшие биты
        }
    }
    
    // Обрезаем длинную последовательность для вывода
    String displayData = bitSequence;
    if (displayData.length() > 30) {
        displayData = displayData.substring(0, 27) + "...";
    }
    
    // Для Nero Radio показываем дополнительную информацию
    if (protocolName == "Nero Radio") {
        Serial.printf("[CC1101] 🔑 Ключ: %s (56-bit) | Код: %lu (0x%s) | Битовая строка: %s | RSSI: %d dBm | TE: %.0f мкс | Частота: %.2f МГц\n",
                      lastKey.protocol.c_str(), lastKey.code, hexCode.c_str(), 
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

    // Настройка модуляции AM650 (как в Flipper Zero)
    state = cc->setOOK(true);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] ⚠️ Ошибка установки OOK модуляции");
    }

    // Параметры для AM650 (ASK/OOK модуляция)
    // Битрейт: 3.79 kbps для стандартных протоколов
    state = cc->setBitRate(3.79);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] Битрейт: 3.79 kbps (AM650)");
    }

    // Ширина полосы RX: 58 кГц (оптимально для AM650)
    state = cc->setRxBandwidth(58.0);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] Ширина полосы RX: 58 кГц (AM650)");
    }

    // Девиация частоты: 5.2 кГц для AM650
    state = cc->setFrequencyDeviation(5.2);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] Девиация частоты: 5.2 кГц (AM650)");
    }
    
    // Дополнительные настройки для лучшей фильтрации шумов
    // Увеличиваем порог RSSI для более строгой фильтрации
    // Это делается через установку AGC (Automatic Gain Control)

    state = cc->setOutputPower(10);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[CC1101] Выходная мощность: 10 dBm");
    }

    if (!configureForRawMode()) {
        Serial.println("[CC1101] ❌ Не удалось настроить RAW режим");
        return false;
    }

    printConfig();
    
    // Сохраняем время инициализации для фильтрации начальных сигналов
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

    // Фильтруем импульсы за пределами разумных границ
    if (delta < MIN_PULSE_US || delta > MAX_PULSE_US) {
        // Если это очень большой промежуток после начала захвата, возможно конец сигнала
        if (rawSignalIndex >= MIN_PULSES_TO_ACCEPT && delta > END_GAP_US) {
            rawSignalReady = true;
            receivedFlag = true;
            detachRawInterrupt();
            return;
        }
        // Иначе сбрасываем буфер
        if (delta > MAX_PULSE_US) {
            rawSignalIndex = 0;
            firstEdgeCaptured = false;
        }
        lastSignalLevel = level;
        return;
    }

    // Сохраняем длительность ПРЕДЫДУЩЕГО уровня (до изменения)
    if (rawSignalIndex < MAX_RAW_SIGNAL_LENGTH - 1) {
        rawSignalTimings[rawSignalIndex] = delta;
        rawSignalLevels[rawSignalIndex] = lastSignalLevel; // Сохраняем предыдущий уровень
        rawSignalIndex++;
    }

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

