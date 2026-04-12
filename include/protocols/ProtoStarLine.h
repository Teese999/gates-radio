#pragma once
#include "SubGhzDecoderBase.h"

// Star Line (64 bit) — from Unleashed firmware
// TE: 400/800, delta: 150
// Preamble: 4+ short pairs, then gap ~TE_S*7
class ProtoStarLine : public SubGhzDecoderBase {
    enum { Reset, CheckPreamble, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 400;
    static constexpr unsigned long TE_L = 800;
    static constexpr unsigned long TE_D = 150;

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "Star Line"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) { headerCount = 1; state = CheckPreamble; }
            break;
        case CheckPreamble:
            if (durationCheck(duration, TE_S, TE_D)) headerCount++;
            else if (!level && DURATION_DIFF(duration, TE_S * 7) < TE_D * 7 && headerCount >= 4) {
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
                } else if (duration > TE_S * 3) {
                    if (bits >= 64) emitResult(data, bits, TE_S, "Star Line");
                    state = Reset;
                } else state = Reset;
                if (bits >= 64) { emitResult(data, bits, TE_S, "Star Line"); state = Reset; }
            } else state = Reset;
            break;
        }
    }
};

// Somfy Telis (56 bit, Manchester) — from Unleashed firmware
// TE: 640/1280, delta: 250
// Preamble: HIGH ~2560us + LOW ~2560us + sync HIGH ~4850us
class ProtoSomfyTelis : public SubGhzDecoderBase {
    enum { Reset, FoundPre1, FoundPre2, DecoderData } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int manState = 0;
    bool lastManBit = false;

    static constexpr unsigned long TE_S = 640;
    static constexpr unsigned long TE_L = 1280;
    static constexpr unsigned long TE_D = 250;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState == 0) {
            if (isShort) { manState = 1; lastManBit = isHigh; return false; }
            else { outBit = isHigh; return true; }
        } else {
            if (isShort) { outBit = lastManBit; manState = 0; return true; }
            else { manState = 0; return false; }
        }
    }

public:
    void reset() override { state = Reset; data = 0; bits = 0; manState = 0; clearResult(); }
    const char* name() const override { return "Somfy Telis"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S * 4) < TE_D * 2) state = FoundPre1;
            break;
        case FoundPre1:
            if (!level && DURATION_DIFF(duration, TE_S * 4) < TE_D * 2) state = FoundPre2;
            else state = Reset;
            break;
        case FoundPre2:
            if (level && DURATION_DIFF(duration, TE_S * 7.6) < 1000) {
                data = 0; bits = 0; manState = 0; state = DecoderData;
            } else state = Reset;
            break;
        case DecoderData:
            if (!level) {
                if (durationCheck(duration, TE_S, TE_D)) {
                    bool b; if (manFeed(true, false, b)) { data = (data << 1) | b; bits++; }
                } else if (durationCheck(duration, TE_L, TE_D)) {
                    bool b; if (manFeed(false, false, b)) { data = (data << 1) | b; bits++; }
                } else {
                    if (bits >= 56) emitResult(data, bits, TE_S, "Somfy Telis");
                    state = Reset;
                }
            } else {
                if (durationCheck(duration, TE_S, TE_D)) {
                    bool b; if (manFeed(true, true, b)) { data = (data << 1) | b; bits++; }
                } else if (durationCheck(duration, TE_L, TE_D)) {
                    bool b; if (manFeed(false, true, b)) { data = (data << 1) | b; bits++; }
                } else state = Reset;
            }
            if (bits >= 56) { emitResult(data, bits, TE_S, "Somfy Telis"); state = Reset; }
            break;
        }
    }
};

// BFT Mitto (52 bit) — from Unleashed firmware
// TE: 250/500, delta: 100
// Preamble: 6+ short alternating pulses, then LOW ~TE_S*10
class ProtoBftMitto : public SubGhzDecoderBase {
    enum { Reset, CheckPreamble, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 250;
    static constexpr unsigned long TE_L = 500;
    static constexpr unsigned long TE_D = 100;

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "BFT Mitto"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) { headerCount = 1; state = CheckPreamble; }
            break;
        case CheckPreamble:
            if (durationCheck(duration, TE_S, TE_D)) headerCount++;
            else if (!level && DURATION_DIFF(duration, TE_S * 10) < TE_D * 10 && headerCount >= 6) {
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
                } else if (duration > TE_S * 10) {
                    if (bits >= 52) emitResult(data, bits, TE_S, "BFT Mitto");
                    state = Reset;
                } else state = Reset;
                if (bits >= 52) { emitResult(data, bits, TE_S, "BFT Mitto"); state = Reset; }
            } else state = Reset;
            break;
        }
    }
};

// Dooya (40 bit) — from Unleashed firmware
// TE: 350/700, delta: 150
// Preamble: HIGH ~TE_S*12, LOW ~TE_S*4
class ProtoDooya : public SubGhzDecoderBase {
    enum { Reset, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 350;
    static constexpr unsigned long TE_L = 700;
    static constexpr unsigned long TE_D = 150;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Dooya"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S * 12) < TE_D * 6) state = FoundPre;
            break;
        case FoundPre:
            if (!level && DURATION_DIFF(duration, TE_S * 4) < TE_D * 3) {
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
                } else if (duration > TE_S * 8) {
                    if (bits >= 40) emitResult(data, bits, TE_S, "Dooya");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// Marantec (49 bit) — from Unleashed firmware
// TE: 800/1600, delta: 400
// Preamble: LOW ~TE_L*8
class ProtoMarantec : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 800;
    static constexpr unsigned long TE_L = 1600;
    static constexpr unsigned long TE_D = 400;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Marantec"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_L * 8) < TE_D * 8) {
                data = 0; bits = 0; state = SaveDur;
            }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else {
                if (bits >= 49) emitResult(data, bits, TE_S, "Marantec");
                state = Reset;
            }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (duration > TE_L * 4) {
                    if (bits >= 49) emitResult(data, bits, TE_S, "Marantec");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};
