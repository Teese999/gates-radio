#pragma once
#include "SubGhzDecoderBase.h"

// Nice FLO (12/24 bit) — from Unleashed firmware
// TE: 700/1400, delta: 200
// Preamble: LOW ~ 36*700 = 25200us
class ProtoNiceFlo : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 700;
    static constexpr unsigned long TE_L = 1400;
    static constexpr unsigned long TE_D = 200;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Nice FLO"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 36) < TE_D * 36) {
                state = FoundStart; data = 0; bits = 0;
            }
            break;
        case FoundStart:
            if (level && durationCheck(duration, TE_S, TE_D)) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (savedDur > TE_S * 4) {
                    if (bits >= 12) emitResult(data, bits, TE_S, "Nice FLO");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Nice FlorS (52/72 bit) — from Unleashed firmware
// TE: 500/1000, delta: 300
// Preamble: LOW ~38*500=19000, then HIGH ~3*500=1500, then LOW ~3*500=1500
class ProtoNiceFlorS : public SubGhzDecoderBase {
    enum { Reset, CheckHeader, FoundHeader, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    uint64_t data_2 = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 500;
    static constexpr unsigned long TE_L = 1000;
    static constexpr unsigned long TE_D = 300;

public:
    void reset() override { state = Reset; data = 0; data_2 = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Nice FlorS"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 38) < TE_D * 38) {
                state = CheckHeader;
            }
            break;
        case CheckHeader:
            if (level && DURATION_DIFF(duration, TE_S * 3) < TE_D * 3) {
                state = FoundHeader;
            } else state = Reset;
            break;
        case FoundHeader:
            if (!level && DURATION_DIFF(duration, TE_S * 3) < TE_D * 3) {
                data = 0; data_2 = 0; bits = 0; state = SaveDur;
            } else state = Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else {
                // End marker: HIGH ~3*TE_S
                if (bits >= 52) {
                    result = {true, data, data_2, bits, TE_S, bits >= 72 ? "Nice One" : "Nice FlorS"};
                }
                state = Reset;
            }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    if (bits < 52) data = (data << 1);
                    else data_2 = (data_2 << 1);
                    bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    if (bits < 52) data = (data << 1) | 1;
                    else data_2 = (data_2 << 1) | 1;
                    bits++; state = SaveDur;
                } else if (DURATION_DIFF(savedDur, TE_S * 3) < TE_D * 3) {
                    // End: HIGH ~3*TE_S
                    if (bits >= 52) {
                        result = {true, data, data_2, bits, TE_S, bits >= 72 ? "Nice One" : "Nice FlorS"};
                    }
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};
