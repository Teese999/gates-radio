#pragma once
#include "SubGhzDecoderBase.h"

// Princeton (24 bit) — from Unleashed firmware
// TE: 390/1170 (1:3), delta: 300
// Preamble: LOW ~ 36*TE_S = 14040us
class ProtoPrinceton : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;
    unsigned long teSum = 0;

    static constexpr unsigned long TE_S = 390;
    static constexpr unsigned long TE_L = 1170;
    static constexpr unsigned long TE_D = 300;

public:
    void reset() override { state = Reset; data = 0; bits = 0; teSum = 0; clearResult(); }
    const char* name() const override { return "Princeton"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 36) < TE_D * 36) {
                state = SaveDur; data = 0; bits = 0; teSum = 0;
            }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else {
                if (bits >= 24) {
                    float te = teSum / (float)(bits * 2);
                    emitResult(data, bits, te, "Princeton");
                }
                state = Reset;
            }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D * 3)) {
                    data = (data << 1); bits++; teSum += savedDur; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D * 3) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; teSum += duration; state = SaveDur;
                } else if (duration > TE_S * 10) {
                    if (bits >= 24) {
                        float te = teSum / (float)bits;
                        emitResult(data, bits, te, "Princeton");
                    }
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};
