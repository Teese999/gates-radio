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

    // Unleashed: TE_short=200, TE_long=400, delta=80
    // Real signal: short~200-210, long~410-420
    static constexpr unsigned long TE_S = 200;
    static constexpr unsigned long TE_L = 400;
    static constexpr unsigned long TE_D = 100; // Wider than Unleashed (80) for CC1101 jitter

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
            // Any non-short pulse after preamble = transition to data
            // Real signal: preamble is all ~200us, data starts with ~410us pulses
            else if (headerCount >= 6) {
                data = 0; bits = 0;
                // This longer pulse is the first data element — process it
                if (level && durationCheck(duration, TE_L, TE_D)) {
                    savedDur = duration; state = CheckDur; // LONG HIGH, wait for LOW
                } else {
                    state = SaveDur; // Start bit or gap, wait for first HIGH
                }
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
