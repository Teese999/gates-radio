#pragma once
#include "SubGhzDecoderBase.h"

// CAME (12/18/24/25/42 bit) — from Unleashed firmware
// TE: 320/640, delta: 150
// Preamble: LOW ~ 56*320 = 17920us
class ProtoCAME : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    unsigned long te = 0;

    static constexpr unsigned long TE_S = 320;
    static constexpr unsigned long TE_L = 640;
    static constexpr unsigned long TE_D = 150;

public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "CAME"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S * 56) < TE_D * 63) {
                state = FoundStart; data = 0; bits = 0;
            }
            break;
        case FoundStart:
            if (level && durationCheck(duration, TE_S, TE_D)) { te = duration; state = CheckDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (level) { te = duration; state = CheckDur; }
            else state = Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(te, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    data = (data << 1); bits++; state = SaveDur;
                } else if (durationCheck(te, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    data = (data << 1) | 1; bits++; state = SaveDur;
                } else if (duration >= TE_S * 4) {
                    if (bits == 12 || bits == 18 || bits == 24 || bits == 25 || bits == 42)
                        emitResult(data, bits, TE_S, "CAME");
                    state = Reset;
                } else state = Reset;
            } else state = Reset;
            break;
        }
    }
};

// CAME TWEE (54 bit, Manchester) — from Unleashed
// TE: 500/1000, delta: 250, Preamble: LOW ~51000 or ~12000
class ProtoCameTwee : public SubGhzDecoderBase {
    enum { Reset, DecoderData } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    bool lastManBit = false;
    int manState = 0; // 0=idle, 1=gotShort

    static constexpr unsigned long TE_S = 500;
    static constexpr unsigned long TE_L = 1000;
    static constexpr unsigned long TE_D = 250;

    void manReset() { manState = 0; }

    // Simple Manchester decoder
    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState == 0) {
            if (isShort) { manState = 1; lastManBit = isHigh; return false; }
            else { outBit = !isHigh; return true; } // Long = full bit
        } else {
            if (isShort) { outBit = !lastManBit; manState = 0; return true; }
            else { manState = 0; return false; } // Error
        }
    }

public:
    void reset() override { state = Reset; data = 0; bits = 0; manReset(); clearResult(); }
    const char* name() const override { return "CAME Twee"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && (DURATION_DIFF(duration, 51000) < 5000 || DURATION_DIFF(duration, 12000) < 2500)) {
                state = DecoderData; data = 0; bits = 0; manReset();
            }
            break;
        case DecoderData:
            if (!level) {
                if (durationCheck(duration, TE_S, TE_D)) {
                    bool bit; if (manFeed(true, false, bit)) { data = (data << 1) | (!bit); bits++; }
                } else if (durationCheck(duration, TE_L, TE_D)) {
                    bool bit; if (manFeed(false, false, bit)) { data = (data << 1) | (!bit); bits++; }
                } else if (duration > TE_L * 2 + TE_D) {
                    if (bits >= 54) emitResult(data, bits, TE_S, "CAME Twee");
                    state = Reset;
                } else state = Reset;
            } else {
                if (durationCheck(duration, TE_S, TE_D)) {
                    bool bit; if (manFeed(true, true, bit)) { data = (data << 1) | (!bit); bits++; }
                } else if (durationCheck(duration, TE_L, TE_D)) {
                    bool bit; if (manFeed(false, true, bit)) { data = (data << 1) | (!bit); bits++; }
                } else state = Reset;
            }
            if (bits >= 54) { emitResult(data, bits, TE_S, "CAME Twee"); state = Reset; }
            break;
        }
    }
};

// CAME Atomo (62 bit, Manchester) — from Unleashed
// TE: 600/1200, delta: 250, Preamble: LOW ~7200 or ~72000
class ProtoCameAtomo : public SubGhzDecoderBase {
    enum { Reset, DecoderData } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int manState = 0;
    bool lastManBit = false;

    static constexpr unsigned long TE_S = 600;
    static constexpr unsigned long TE_L = 1200;
    static constexpr unsigned long TE_D = 250;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState == 0) {
            if (isShort) { manState = 1; lastManBit = isHigh; return false; }
            else { outBit = !isHigh; return true; }
        } else {
            if (isShort) { outBit = !lastManBit; manState = 0; return true; }
            else { manState = 0; return false; }
        }
    }

public:
    void reset() override { state = Reset; data = 0; bits = 0; manState = 0; clearResult(); }
    const char* name() const override { return "CAME Atomo"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && (DURATION_DIFF(duration, TE_L*10) < 5000 || DURATION_DIFF(duration, TE_L*60) < 10000)) {
                state = DecoderData; data = 0; bits = 1; manState = 0;
            }
            break;
        case DecoderData:
            if (!level) {
                if (durationCheck(duration, TE_S, TE_D)) { bool b; if (manFeed(true,false,b)) { data=(data<<1)|(!b); bits++; } }
                else if (durationCheck(duration, TE_L, TE_D)) { bool b; if (manFeed(false,false,b)) { data=(data<<1)|(!b); bits++; } }
                else if (duration >= TE_L*2+TE_D) {
                    if (bits >= 62) emitResult(data, bits, TE_S, "CAME Atomo");
                    state = Reset;
                } else state = Reset;
            } else {
                if (durationCheck(duration, TE_S, TE_D)) { bool b; if (manFeed(true,true,b)) { data=(data<<1)|(!b); bits++; } }
                else if (durationCheck(duration, TE_L, TE_D)) { bool b; if (manFeed(false,true,b)) { data=(data<<1)|(!b); bits++; } }
                else state = Reset;
            }
            if (bits >= 62) { emitResult(data, bits, TE_S, "CAME Atomo"); state = Reset; }
            break;
        }
    }
};
