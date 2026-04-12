#pragma once
#include "SubGhzDecoderBase.h"

// Nero Radio (56 bit) — from Unleashed firmware
// TE: 200/400, delta: 80
// Preamble: 49+ alternating short pulses, then start bit HIGH ~4*TE_S
class ProtoNeroRadio : public SubGhzDecoderBase {
    enum { Reset, CheckPreamble, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDur = 0;

    // Real signal has TE ~230-330 for short, ~430-760 for long
    static constexpr unsigned long TE_S = 250;
    static constexpr unsigned long TE_L = 500;
    static constexpr unsigned long TE_D = 150; // Wide tolerance for real signals

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "Nero Radio"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Any short pulse (H or L) could start preamble
            if (durationCheck(duration, TE_S, TE_D)) {
                headerCount = 1; state = CheckPreamble;
            }
            break;
        case CheckPreamble:
            if (durationCheck(duration, TE_S, TE_D)) {
                headerCount++;
            }
            // Start bit: longer pulse ~3-4*TE_S after enough preamble pulses
            else if (duration > TE_S * 2 && duration < TE_S * 6 && headerCount > 10) {
                data = 0; bits = 0; state = SaveDur;
            } else if (headerCount < 10) {
                state = Reset; // Not enough preamble
            } else state = Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (duration > TE_S * 10 + TE_D * 2) {
                    if (bits >= 56) emitResult(data, bits, TE_S, "Nero Radio");
                    state = Reset;
                } else state = Reset;
                if (bits >= 56) { emitResult(data, bits, TE_S, "Nero Radio"); state = Reset; }
            } else state = Reset;
            break;
        }
    }
};

// Nero Sketch (40 bit) — from Unleashed firmware
// TE: 330/660, delta: 150
// Preamble: LOW ~33*330 = 10890us
class ProtoNeroSketch : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 330;
    static constexpr unsigned long TE_L = 660;
    static constexpr unsigned long TE_D = 150;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Nero Sketch"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 33) < TE_D * 33) {
                state = FoundStart; data = 0; bits = 0;
            }
            break;
        case FoundStart:
            if (level && durationCheck(duration, TE_S, TE_D)) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (duration >= TE_S * 4) {
                    if (bits >= 40) emitResult(data, bits, TE_S, "Nero Sketch");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};
