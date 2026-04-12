#ifndef SUBGHZ_DECODER_BASE_H
#define SUBGHZ_DECODER_BASE_H

#include <Arduino.h>

// ============================================================================
// Flipper Zero SubGhz Decoder Base — ported from Unleashed firmware
// Each protocol is a state machine processing pulses one at a time.
// All decoders run in parallel via SubGhzMultiDecoder.
// ============================================================================

// Duration check macro (same as DURATION_DIFF in Flipper)
#define DURATION_DIFF(x, y) (((x) > (y)) ? ((x) - (y)) : ((y) - (x)))

static inline bool durationCheck(unsigned long duration, unsigned long expected, unsigned long tolerance) {
    return DURATION_DIFF(duration, expected) < tolerance;
}

// Decoder result
struct SubGhzDecoderResult {
    bool ready;
    uint64_t data;        // Up to 64 bits of decoded data
    uint64_t data_2;      // Extra data for protocols > 64 bits (e.g., Security+ v2)
    int bitCount;
    float te;
    const char* protocol;
};

// Base class for all protocol decoders
class SubGhzDecoderBase {
public:
    virtual ~SubGhzDecoderBase() = default;
    virtual void reset() = 0;
    virtual void feed(bool level, unsigned long duration) = 0;
    virtual SubGhzDecoderResult getResult() const = 0;
    virtual const char* name() const = 0;

protected:
    SubGhzDecoderResult result{};

    void emitResult(uint64_t data, int bits, float te, const char* proto) {
        result = {true, data, 0, bits, te, proto};
    }

    void clearResult() {
        result.ready = false;
    }
};

// ============================================================================
// Multi-decoder: feeds all decoders in parallel, returns first match
// ============================================================================
template<int MAX_DECODERS>
class SubGhzMultiDecoderT {
    SubGhzDecoderBase* decoders[MAX_DECODERS]{};
    int count = 0;

public:
    void add(SubGhzDecoderBase* decoder) {
        if (count < MAX_DECODERS) decoders[count++] = decoder;
    }

    void resetAll() {
        for (int i = 0; i < count; i++) decoders[i]->reset();
    }

    SubGhzDecoderResult feed(bool level, unsigned long duration) {
        for (int i = 0; i < count; i++) {
            decoders[i]->feed(level, duration);
            SubGhzDecoderResult r = decoders[i]->getResult();
            if (r.ready) {
                resetAll();
                return r;
            }
        }
        return {false, 0, 0, 0, 0, nullptr};
    }

    int getCount() const { return count; }
};

#endif // SUBGHZ_DECODER_BASE_H
