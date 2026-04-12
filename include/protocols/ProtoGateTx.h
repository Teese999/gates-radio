#pragma once
#include "SubGhzDecoderBase.h"

// Gate TX (24 bit) — from Unleashed firmware
// TE: 350/700, delta: 150
// Preamble: LOW ~ 36*TE_S
class ProtoGateTx : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 350;
    static constexpr unsigned long TE_L = 700;
    static constexpr unsigned long TE_D = 150;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Gate TX"; }
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
                    if (bits >= 24) emitResult(data, bits, TE_S, "Gate TX");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Holtek HT12x (12 bit) — from Unleashed firmware
// TE: 500/1000, delta: 200
// Preamble: LOW ~ 36*TE_S = 18000us
class ProtoHoltek : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 500;
    static constexpr unsigned long TE_L = 1000;
    static constexpr unsigned long TE_D = 200;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Holtek"; }
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
                    if (bits >= 12) emitResult(data, bits, TE_S, "Holtek");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Linear (10 bit) — from Unleashed firmware
// TE: 500/1500 (1:3), delta: 250
// Preamble: none (very short protocol, detected by timing)
class ProtoLinear : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 500;
    static constexpr unsigned long TE_L = 1500;
    static constexpr unsigned long TE_D = 250;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Linear"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 39) < TE_D * 39) {
                state = SaveDur; data = 0; bits = 0;
            }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else {
                if (bits >= 10) emitResult(data, bits, TE_S, "Linear");
                state = Reset;
            }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D * 3)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D * 3) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (duration > TE_S * 10) {
                    if (bits >= 10) emitResult(data, bits, TE_S, "Linear");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Chamberlain (9 bit) — from Unleashed firmware
// TE: 1000/3000, delta: 250
// Preamble: HIGH ~39*TE_S
class ProtoChamberlain : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 1000;
    static constexpr unsigned long TE_L = 3000;
    static constexpr unsigned long TE_D = 250;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Chamberlain"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S * 39) < TE_D * 10) {
                state = SaveDur; data = 0; bits = 0;
            }
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else {
                if (bits >= 9) emitResult(data, bits, TE_S, "Chamberlain");
                state = Reset;
            }
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Hormann BiSecur (44 bit) — from Unleashed firmware
// TE: 500/1000, delta: 200
// Preamble: HIGH ~24*TE_S then LOW ~TE_S
class ProtoHormann : public SubGhzDecoderBase {
    enum { Reset, FoundPreamble, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 500;
    static constexpr unsigned long TE_L = 1000;
    static constexpr unsigned long TE_D = 200;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Hormann"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S * 24) < TE_D * 24) {
                state = FoundPreamble;
            }
            break;
        case FoundPreamble:
            if (!level && durationCheck(duration, TE_S, TE_D)) {
                data = 0; bits = 0; state = SaveDur;
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
                } else if (duration > TE_S * 4) {
                    if (bits >= 44) emitResult(data, bits, TE_S, "Hormann");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// FAAC SLH (64 bit) — from Unleashed firmware
// TE: 255/510, delta: 100
// Preamble: HIGH ~TE_S*2, LOW ~TE_L*2
class ProtoFaacSlh : public SubGhzDecoderBase {
    enum { Reset, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 255;
    static constexpr unsigned long TE_L = 510;
    static constexpr unsigned long TE_D = 100;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "FAAC SLH"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S * 2) < TE_D * 2) state = FoundPre;
            break;
        case FoundPre:
            if (!level && DURATION_DIFF(duration, TE_L * 2) < TE_D * 2) {
                data = 0; bits = 0; state = SaveDur;
            } else state = Reset;
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
                } else if (duration > TE_L * 4) {
                    if (bits >= 64) emitResult(data, bits, TE_S, "FAAC SLH");
                    state = Reset;
                } else state = Reset;
                if (bits >= 64) { emitResult(data, bits, TE_S, "FAAC SLH"); state = Reset; }
            } else state = Reset;
            break;
        }
    }
};
