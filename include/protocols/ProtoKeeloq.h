#pragma once
#include "SubGhzDecoderBase.h"

// Keeloq (64 bit) — from Unleashed firmware
// TE: 400/800, delta: 140
// Preamble: 3+ short pulses, then gap ~10*TE_S
class ProtoKeeloq : public SubGhzDecoderBase {
    enum { Reset, CheckPreamble, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0;
    int bits = 0;
    int headerCount = 0;
    unsigned long savedDur = 0;

    static constexpr unsigned long TE_S = 400;
    static constexpr unsigned long TE_L = 800;
    static constexpr unsigned long TE_D = 140;

public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "Keeloq"; }
    SubGhzDecoderResult getResult() const override { return result; }

    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) {
                headerCount = 1; state = CheckPreamble;
            }
            break;
        case CheckPreamble:
            if (durationCheck(duration, TE_S, TE_D)) {
                headerCount++;
            } else if (!level && DURATION_DIFF(duration, TE_S * 10) < TE_D * 10 && headerCount >= 3) {
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
                } else if (duration > TE_S * 2 + TE_D) {
                    if (bits >= 64 && bits <= 66) emitResult(data, bits, TE_S, "Keeloq");
                    state = Reset;
                } else state = Reset;
                if (bits >= 66) {
                    if (bits >= 64) emitResult(data, bits, TE_S, "Keeloq");
                    state = Reset;
                }
            } else state = Reset;
            break;
        }
    }
};
