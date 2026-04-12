#ifndef SUBGHZ_DECODER_H
#define SUBGHZ_DECODER_H

#include <Arduino.h>

// Результат декодирования
struct DecoderResult {
    bool ready;           // Пакет полностью декодирован
    uint64_t data;        // Декодированные данные (до 64 бит)
    int bitCount;         // Количество декодированных бит
    float te;             // Измеренный TE
    const char* protocol; // Имя протокола
};

// Макрос допуска (как DURATION_DIFF в Flipper)
static inline bool durationInRange(unsigned long duration, unsigned long expected, unsigned long delta) {
    return (duration > (expected > delta ? expected - delta : 0)) &&
           (duration < expected + delta);
}

// ============================================================================
// Базовый класс протокольного декодера (Flipper Zero architecture)
// Каждый декодер — state machine, обрабатывающий импульсы по одному.
// ============================================================================
class ProtocolDecoder {
public:
    virtual ~ProtocolDecoder() = default;
    virtual void reset() = 0;
    virtual void feed(bool level, unsigned long duration) = 0;
    virtual DecoderResult getResult() const = 0;
    virtual const char* name() const = 0;
};

// ============================================================================
// CAME (12/18/24/25 бит) — TE=320, ratio 1:2, preamble = LOW ~17920мкс
// ============================================================================
class CameDecoder : public ProtocolDecoder {
    enum State { Reset, FoundStart, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDuration = 0;
    float te = 0;

    static constexpr unsigned long TE_SHORT = 320;
    static constexpr unsigned long TE_LONG = 640;
    static constexpr unsigned long TE_DELTA = 150;

    DecoderResult result{};

public:
    void reset() override {
        state = Reset; data = 0; bits = 0; result.ready = false;
    }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Ищем преамбулу: LOW ~ 56*TE_SHORT (17920мкс) ± 63*TE_DELTA
            if (!level && durationInRange(duration, TE_SHORT * 56, TE_DELTA * 63)) {
                state = FoundStart;
                data = 0; bits = 0;
            }
            break;

        case FoundStart:
            // Ждём первый HIGH
            if (level && durationInRange(duration, TE_SHORT, TE_DELTA)) {
                savedDuration = duration;
                state = CheckDuration;
            } else {
                state = Reset;
            }
            break;

        case SaveDuration:
            if (level) {
                savedDuration = duration;
                state = CheckDuration;
            } else {
                state = Reset;
            }
            break;

        case CheckDuration:
            if (!level) {
                // short HIGH + long LOW = bit 0
                if (durationInRange(savedDuration, TE_SHORT, TE_DELTA) &&
                    durationInRange(duration, TE_LONG, TE_DELTA)) {
                    data = (data << 1);
                    bits++;
                    state = SaveDuration;
                }
                // long HIGH + short LOW = bit 1
                else if (durationInRange(savedDuration, TE_LONG, TE_DELTA) &&
                         durationInRange(duration, TE_SHORT, TE_DELTA)) {
                    data = (data << 1) | 1;
                    bits++;
                    state = SaveDuration;
                }
                // Gap — конец пакета
                else if (duration > TE_SHORT * 4) {
                    if (bits == 12 || bits == 18 || bits == 24 || bits == 25) {
                        te = TE_SHORT;
                        result = {true, data, bits, te, "CAME"};
                    }
                    state = Reset;
                }
                else {
                    state = Reset;
                }
            } else {
                state = Reset;
            }
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "CAME"; }
};

// ============================================================================
// Nice FLO (12/24 бит) — TE=700, ratio 1:2, preamble = LOW ~25200мкс
// ============================================================================
class NiceFloDecoder : public ProtocolDecoder {
    enum State { Reset, FoundStart, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDuration = 0;

    static constexpr unsigned long TE_SHORT = 700;
    static constexpr unsigned long TE_LONG = 1400;
    static constexpr unsigned long TE_DELTA = 200;

    DecoderResult result{};

public:
    void reset() override { state = Reset; data = 0; bits = 0; result.ready = false; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && durationInRange(duration, TE_SHORT * 36, TE_DELTA * 36)) {
                state = FoundStart;
                data = 0; bits = 0;
            }
            break;

        case FoundStart:
            if (level && durationInRange(duration, TE_SHORT, TE_DELTA)) {
                savedDuration = duration;
                state = CheckDuration;
            } else { state = Reset; }
            break;

        case SaveDuration:
            if (!level) {
                savedDuration = duration;
                state = CheckDuration;
            } else { state = Reset; }
            break;

        case CheckDuration:
            if (level) {
                if (durationInRange(savedDuration, TE_SHORT, TE_DELTA) &&
                    durationInRange(duration, TE_LONG, TE_DELTA)) {
                    data = (data << 1);
                    bits++;
                    state = SaveDuration;
                } else if (durationInRange(savedDuration, TE_LONG, TE_DELTA) &&
                           durationInRange(duration, TE_SHORT, TE_DELTA)) {
                    data = (data << 1) | 1;
                    bits++;
                    state = SaveDuration;
                } else if (savedDuration > TE_SHORT * 4) {
                    if (bits >= 12) {
                        result = {true, data, bits, (float)TE_SHORT, "Nice FLO"};
                    }
                    state = Reset;
                } else { state = Reset; }
            } else { state = Reset; }
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "Nice FLO"; }
};

// ============================================================================
// Princeton (24 бит) — TE=390, ratio 1:3, preamble = LOW ~36*TE
// ============================================================================
class PrincetonDecoder : public ProtocolDecoder {
    enum State { Reset, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDuration = 0;
    unsigned long teSum = 0;

    static constexpr unsigned long TE_SHORT = 390;
    static constexpr unsigned long TE_LONG = 1170;
    static constexpr unsigned long TE_DELTA = 300;

    DecoderResult result{};

public:
    void reset() override { state = Reset; data = 0; bits = 0; teSum = 0; result.ready = false; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Preamble: LOW ~ 36*TE
            if (!level && durationInRange(duration, TE_SHORT * 36, TE_DELTA * 36)) {
                state = SaveDuration;
                data = 0; bits = 0; teSum = 0;
            }
            break;

        case SaveDuration:
            if (level) {
                savedDuration = duration;
                state = CheckDuration;
            } else {
                // Ещё одна длинная пауза — может быть повтор преамбулы
                if (bits >= 24) {
                    float te = teSum / (float)(bits * 2);
                    result = {true, data, bits, te, "Princeton"};
                }
                state = Reset;
            }
            break;

        case CheckDuration:
            if (!level) {
                // short HIGH + long LOW = bit 0
                if (durationInRange(savedDuration, TE_SHORT, TE_DELTA) &&
                    durationInRange(duration, TE_LONG, TE_DELTA * 3)) {
                    data = (data << 1);
                    bits++;
                    teSum += savedDuration + duration;
                    state = SaveDuration;
                }
                // long HIGH + short LOW = bit 1
                else if (durationInRange(savedDuration, TE_LONG, TE_DELTA * 3) &&
                         durationInRange(duration, TE_SHORT, TE_DELTA)) {
                    data = (data << 1) | 1;
                    bits++;
                    teSum += savedDuration + duration;
                    state = SaveDuration;
                }
                // Gap — конец
                else if (duration > TE_SHORT * 10) {
                    if (bits >= 24) {
                        float te = teSum / (float)(bits * 2);
                        result = {true, data, bits, te, "Princeton"};
                    }
                    state = Reset;
                }
                else { state = Reset; }
            } else { state = Reset; }
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "Princeton"; }
};

// ============================================================================
// Nero Radio (56 бит)
// Реальные тайминги с CC1101: TE_short≈230-260мкс, TE_long≈750-800мкс (ratio ~1:3)
// Преамбула: серия коротких импульсов HIGH~230 + LOW~230, затем start gap ~1600мкс
// ============================================================================
class NeroRadioDecoder : public ProtocolDecoder {
    enum State { Reset, CheckPreamble, WaitStartGap, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDuration = 0;

    // Реальные тайминги из перехваченного сигнала
    static constexpr unsigned long TE_SHORT = 240;
    static constexpr unsigned long TE_LONG = 780;
    static constexpr unsigned long TE_DELTA = 120; // Широкий допуск для реальных сигналов

    DecoderResult result{};

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; result.ready = false; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Ищем короткий HIGH (~230мкс) — начало преамбулы
            if (level && durationInRange(duration, TE_SHORT, TE_DELTA)) {
                headerCount = 1;
                state = CheckPreamble;
            }
            break;

        case CheckPreamble:
            // Преамбула: чередование коротких HIGH/LOW
            if (durationInRange(duration, TE_SHORT, TE_DELTA)) {
                headerCount++;
            }
            // Start gap: длинная пауза ~1600мкс после преамбулы
            else if (!level && duration > TE_SHORT * 4 && headerCount >= 6) {
                data = 0; bits = 0;
                state = SaveDuration;
            }
            // Длинный LOW может быть начало данных если была хоть какая-то преамбула
            else if (!level && durationInRange(duration, TE_LONG, TE_DELTA) && headerCount >= 4) {
                data = 0; bits = 0;
                // Первый bit: преамбула HIGH + этот LONG LOW = bit
                state = SaveDuration;
            }
            else {
                state = Reset;
            }
            break;

        case SaveDuration:
            if (level) {
                savedDuration = duration;
                state = CheckDuration;
            } else {
                // Два LOW подряд — может быть часть данных
                if (durationInRange(duration, TE_LONG, TE_DELTA) && bits > 0) {
                    // LOW без предыдущего HIGH — пропускаем
                }
                state = Reset;
            }
            break;

        case CheckDuration:
            if (!level) {
                // bit 1: short HIGH (~240) + long LOW (~780)
                if (durationInRange(savedDuration, TE_SHORT, TE_DELTA) &&
                    durationInRange(duration, TE_LONG, TE_DELTA)) {
                    data = (data << 1) | 1;
                    bits++;
                    state = SaveDuration;
                }
                // bit 0: long HIGH (~780) + short LOW (~240)
                else if (durationInRange(savedDuration, TE_LONG, TE_DELTA) &&
                         durationInRange(duration, TE_SHORT, TE_DELTA)) {
                    data = (data << 1);
                    bits++;
                    state = SaveDuration;
                }
                // Stop/gap
                else if (duration > TE_LONG * 2) {
                    if (bits >= 24) { // Принимаем от 24 бит
                        result = {true, data, bits, (float)TE_SHORT, "Nero Radio"};
                    }
                    state = Reset;
                }
                else { state = Reset; }

                // Полный пакет
                if (bits >= 56) {
                    result = {true, data, bits, (float)TE_SHORT, "Nero Radio"};
                    state = Reset;
                }
            } else { state = Reset; }
            break;

        default:
            state = Reset;
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "Nero Radio"; }
};

// ============================================================================
// Keeloq (64 бит) — TE=400, ratio 1:2, preamble = 3+ коротких + gap
// ============================================================================
class KeeloqDecoder : public ProtocolDecoder {
    enum State { Reset, CheckPreamble, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDuration = 0;

    static constexpr unsigned long TE_SHORT = 400;
    static constexpr unsigned long TE_LONG = 800;
    static constexpr unsigned long TE_DELTA = 140;

    DecoderResult result{};

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; result.ready = false; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationInRange(duration, TE_SHORT, TE_DELTA)) {
                headerCount = 1;
                state = CheckPreamble;
            }
            break;

        case CheckPreamble:
            if (durationInRange(duration, TE_SHORT, TE_DELTA)) {
                headerCount++;
            }
            // Gap after preamble: ~10*TE_SHORT
            else if (!level && durationInRange(duration, TE_SHORT * 10, TE_DELTA * 10) && headerCount >= 3) {
                data = 0; bits = 0;
                state = SaveDuration;
            } else { state = Reset; }
            break;

        case SaveDuration:
            if (level) {
                savedDuration = duration;
                state = CheckDuration;
            } else { state = Reset; }
            break;

        case CheckDuration:
            if (!level) {
                // bit 1: short HIGH + long LOW
                if (durationInRange(savedDuration, TE_SHORT, TE_DELTA) &&
                    durationInRange(duration, TE_LONG, TE_DELTA)) {
                    data = (data << 1) | 1;
                    bits++;
                    state = SaveDuration;
                }
                // bit 0: long HIGH + short LOW
                else if (durationInRange(savedDuration, TE_LONG, TE_DELTA) &&
                         durationInRange(duration, TE_SHORT, TE_DELTA)) {
                    data = (data << 1);
                    bits++;
                    state = SaveDuration;
                }
                // End gap
                else if (duration > TE_SHORT * 2 + TE_DELTA) {
                    if (bits >= 64 && bits <= 66) {
                        result = {true, data, bits, (float)TE_SHORT, "Keeloq"};
                    }
                    state = Reset;
                }
                else { state = Reset; }

                if (bits >= 66) {
                    if (bits >= 64) result = {true, data, bits, (float)TE_SHORT, "Keeloq"};
                    state = Reset;
                }
            } else { state = Reset; }
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "Keeloq"; }
};

// ============================================================================
// Generic OOK 1:3 decoder — ловит любой сигнал с ratio ~1:3
// Для протоколов без уникальной преамбулы (CAME на 434МГц, custom remotes)
// Ищет паттерн: SHORT_H+LONG_L или LONG_H+SHORT_L с ratio ~3x
// ============================================================================
class GenericOOKDecoder : public ProtocolDecoder {
    enum State { Reset, SaveDuration, CheckDuration };
    State state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDuration = 0;
    int consecutiveValid = 0;

    DecoderResult result{};

public:
    void reset() override {
        state = Reset; data = 0; bits = 0; consecutiveValid = 0; result.ready = false;
    }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Любой HIGH в разумном диапазоне — начинаем пробовать
            if (level && duration >= 150 && duration <= 2000) {
                savedDuration = duration;
                state = CheckDuration;
                data = 0; bits = 0; consecutiveValid = 0;
            }
            break;

        case SaveDuration:
            if (level) {
                savedDuration = duration;
                state = CheckDuration;
            } else {
                // Gap — конец пакета?
                if (bits >= 12 && consecutiveValid >= 12) {
                    const char* proto = "OOK";
                    if (bits == 12) proto = "CAME 12";
                    else if (bits == 24) proto = "CAME";
                    else if (bits == 28) proto = "EV1527";
                    result = {true, data, bits, (float)(savedDuration > 500 ? savedDuration / 3 : savedDuration), proto};
                }
                state = Reset;
            }
            break;

        case CheckDuration:
            if (!level) {
                // Check ratio ~1:3 (±40%)
                float ratio = (savedDuration > duration)
                    ? (float)savedDuration / duration
                    : (float)duration / savedDuration;

                if (ratio >= 2.0f && ratio <= 4.5f) {
                    // Valid bit pair
                    if (savedDuration < duration) {
                        // short HIGH + long LOW = bit 0
                        data = (data << 1);
                    } else {
                        // long HIGH + short LOW = bit 1
                        data = (data << 1) | 1;
                    }
                    bits++;
                    consecutiveValid++;
                    state = SaveDuration;

                    // Max bits reached
                    if (bits >= 64) {
                        result = {true, data, bits, 0, "OOK64"};
                        state = Reset;
                    }
                }
                // Long gap — end of frame
                else if (duration > 2000 && bits >= 12 && consecutiveValid >= 12) {
                    const char* proto = "OOK";
                    if (bits == 12) proto = "CAME 12";
                    else if (bits == 24) proto = "CAME";
                    else if (bits == 28) proto = "EV1527";
                    result = {true, data, bits, 0, proto};
                    state = Reset;
                }
                else {
                    // Not a valid pair — if we had enough bits, emit
                    if (bits >= 12 && consecutiveValid >= 12) {
                        const char* proto = "OOK";
                        if (bits == 12) proto = "CAME 12";
                        else if (bits == 24) proto = "CAME";
                        else if (bits == 28) proto = "EV1527";
                        result = {true, data, bits, 0, proto};
                    }
                    state = Reset;
                }
            } else {
                // Two HIGHs without LOW — reset
                state = Reset;
            }
            break;
        }
    }

    DecoderResult getResult() const override { return result; }
    const char* name() const override { return "GenericOOK"; }
};

// ============================================================================
// Мультидекодер — запускает все декодеры параллельно (как Flipper Zero)
// ============================================================================
class SubGhzMultiDecoder {
public:
    static constexpr int MAX_DECODERS = 7;

private:
    ProtocolDecoder* decoders[MAX_DECODERS];
    int count = 0;

    // Статические инстансы декодеров (без heap allocation)
    CameDecoder cameDecoder;
    NiceFloDecoder niceFloDecoder;
    PrincetonDecoder princetonDecoder;
    NeroRadioDecoder neroRadioDecoder;
    KeeloqDecoder keeloqDecoder;
    GenericOOKDecoder genericOokDecoder;

public:
    SubGhzMultiDecoder() {
        // Порядок важен: специфичные (с преамбулой) первыми, generic последним
        decoders[0] = &cameDecoder;
        decoders[1] = &niceFloDecoder;
        decoders[2] = &princetonDecoder;
        decoders[3] = &neroRadioDecoder;
        decoders[4] = &keeloqDecoder;
        decoders[5] = &genericOokDecoder;  // Fallback для любого 1:3 OOK
        count = 6;
    }

    void resetAll() {
        for (int i = 0; i < count; i++) decoders[i]->reset();
    }

    // Кормим все декодеры одним импульсом — как Flipper
    // Возвращает указатель на первый сработавший декодер, или nullptr
    DecoderResult feed(bool level, unsigned long duration) {
        for (int i = 0; i < count; i++) {
            decoders[i]->feed(level, duration);
            DecoderResult r = decoders[i]->getResult();
            if (r.ready) {
                // Сбрасываем все декодеры после успешного декодирования
                resetAll();
                return r;
            }
        }
        return {false, 0, 0, 0, nullptr};
    }
};

#endif // SUBGHZ_DECODER_H
