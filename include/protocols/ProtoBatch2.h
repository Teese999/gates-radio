#pragma once
#include "SubGhzDecoderBase.h"

// Clemsa (18 bit) — TE: 385/2695, delta: 150
// Preamble: LOW ~19635us (51*TE_S)
class ProtoClemsa : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 385, TE_L = 2695, TE_D = 150;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Clemsa"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*51) < TE_D*25) { state = FoundStart; data=0; bits=0; }
            break;
        case FoundStart:
            if (level) { savedDur = duration; state = CheckDur; } else state = Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; } else state = Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration > TE_L*2) { if (bits>=18) emitResult(data,bits,TE_S,"Clemsa"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Doitrand (37 bit) — TE: 400/1100, delta: 150
// Preamble: LOW ~24800us (62*TE_S), start bit: HIGH ~800us
class ProtoDoitrand : public SubGhzDecoderBase {
    enum { Reset, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 400, TE_L = 1100, TE_D = 150;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Doitrand"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*62) < TE_D*30) state = FoundPre;
            break;
        case FoundPre:
            if (level && DURATION_DIFF(duration, TE_S*2) < TE_D*3) { data=0; bits=0; state=SaveDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=37) emitResult(data,bits,TE_S,"Doitrand"); state=Reset; }
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration > TE_S*10) { if (bits>=37) emitResult(data,bits,TE_S,"Doitrand"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Phoenix V2 (52 bit) — TE: 427/853, delta: 100
// Preamble: LOW ~25620us (60*TE_S), start bit: HIGH ~2562us (6*TE_S)
class ProtoPhoenixV2 : public SubGhzDecoderBase {
    enum { Reset, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 427, TE_L = 853, TE_D = 100;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Phoenix V2"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*60) < 3000) state = FoundPre;
            break;
        case FoundPre:
            if (level && DURATION_DIFF(duration, TE_S*6) < TE_D*6) { data=0; bits=0; state=SaveDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=52) emitResult(data,bits,TE_S,"Phoenix V2"); state=Reset; }
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else { if (bits>=52) emitResult(data,bits,TE_S,"Phoenix V2"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// Magellan (32 bit) — TE: 200/400, delta: 100
// Preamble: 12+ short alternating pulses, start bit: HIGH ~1200us + LOW ~400us
class ProtoMagellan : public SubGhzDecoderBase {
    enum { Reset, CheckPre, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; int headerCount = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 200, TE_L = 400, TE_D = 100;
public:
    void reset() override { state = Reset; data = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "Magellan"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) { headerCount=1; state=CheckPre; }
            break;
        case CheckPre:
            if (durationCheck(duration, TE_S, TE_D)) headerCount++;
            else if (level && DURATION_DIFF(duration, TE_S*6) < TE_D*3 && headerCount>10) state=FoundPre;
            else state=Reset;
            break;
        case FoundPre:
            if (!level && durationCheck(duration, TE_L, TE_D)) { data=0; bits=0; state=SaveDur; }
            else state=Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (duration > TE_S*20) { if (bits>=32) emitResult(data,bits,TE_S,"Magellan"); state=Reset; }
                else state=Reset;
                if (bits>=32) { emitResult(data,bits,TE_S,"Magellan"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// Legrand (18 bit) — TE: 375/1125, delta: 150
// Preamble: LOW ~6000us (16*TE_S)
class ProtoLegrand : public SubGhzDecoderBase {
    enum { Reset, FirstBit, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 375, TE_L = 1125, TE_D = 150;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Legrand"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*16) < TE_D*8) { state=FirstBit; data=0; bits=0; }
            break;
        case FirstBit:
            if (level && durationCheck(duration, TE_S, TE_D)) { savedDur = duration; state = CheckDur; }
            else state = Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=18) emitResult(data,bits,TE_S,"Legrand"); state=Reset; }
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else { if (bits>=18) emitResult(data,bits,TE_S,"Legrand"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// KingGates Stylo 4K (89 bit) — TE: 400/1100, delta: 140
// Preamble: 12x short pairs, then gap ~2200us
class ProtoKingGates : public SubGhzDecoderBase {
    enum { Reset, CheckPre, CheckStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; uint64_t data_2 = 0;
    int bits = 0; int headerCount = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 400, TE_L = 1100, TE_D = 140;
public:
    void reset() override { state = Reset; data = 0; data_2 = 0; bits = 0; headerCount = 0; clearResult(); }
    const char* name() const override { return "KingGates"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) { headerCount=1; state=CheckPre; }
            break;
        case CheckPre:
            if (durationCheck(duration, TE_S, TE_D)) headerCount++;
            else if (!level && DURATION_DIFF(duration, 2200) < TE_D*5 && headerCount>=12) state=CheckStart;
            else state=Reset;
            break;
        case CheckStart:
            if (level && DURATION_DIFF(duration, TE_S*2) < TE_D*2) { data=0; data_2=0; bits=0; state=SaveDur; }
            else state=Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=89) { result={true,data,data_2,bits,TE_S,"KingGates"}; } state=Reset; }
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    if (bits<53) data=(data<<1)|1; else data_2=(data_2<<1)|1;
                    bits++; state=SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    if (bits<53) data=(data<<1); else data_2=(data_2<<1);
                    bits++; state=SaveDur;
                } else { if (bits>=89) { result={true,data,data_2,bits,TE_S,"KingGates"}; } state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// Ansonic (12 bit) — TE: 555/1111, delta: 200
// Preamble: LOW ~36*TE_S
class ProtoAnsonic : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 555, TE_L = 1111, TE_D = 200;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "Ansonic"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*36) < TE_D*36) { state=FoundStart; data=0; bits=0; }
            break;
        case FoundStart:
            if (level) { savedDur = duration; state = CheckDur; } else state = Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; } else state = Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration >= TE_S*4) { if (bits>=12) emitResult(data,bits,TE_S,"Ansonic"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// SMC5326 (25 bit) — TE: 300/900, delta: 200
// Preamble: LOW ~36*TE_S
class ProtoSMC5326 : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 300, TE_L = 900, TE_D = 200;
public:
    void reset() override { state = Reset; data = 0; bits = 0; clearResult(); }
    const char* name() const override { return "SMC5326"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*36) < TE_D*20) { state=SaveDur; data=0; bits=0; }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=25) emitResult(data,bits,TE_S,"SMC5326"); state=Reset; }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration > TE_S*10) { if (bits>=25) emitResult(data,bits,TE_S,"SMC5326"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Honeywell (64 bit, Manchester) — TE: 143/280, delta: 51
class ProtoHoneywell : public SubGhzDecoderBase {
    enum { Reset, DecoderData } state = Reset;
    uint64_t data = 0; int bits = 0;
    int manState = 0; bool lastManBit = false;
    static constexpr unsigned long TE_S = 143, TE_L = 280, TE_D = 51;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState == 0) { if (isShort) { manState=1; lastManBit=isHigh; return false; } else { outBit=isHigh; return true; } }
        else { if (isShort) { outBit=lastManBit; manState=0; return true; } else { manState=0; return false; } }
    }
public:
    void reset() override { state = Reset; data = 0; bits = 0; manState = 0; clearResult(); }
    const char* name() const override { return "Honeywell"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            // Any short pulse starts Manchester decoding attempt
            if (durationCheck(duration, TE_S, TE_D) || durationCheck(duration, TE_L, TE_D)) {
                data=0; bits=0; manState=0; state=DecoderData;
                // Process this pulse
                bool isShort = durationCheck(duration, TE_S, TE_D);
                bool b; if (manFeed(isShort, level, b)) { data=(data<<1)|b; bits++; }
            }
            break;
        case DecoderData: {
            bool isShort = durationCheck(duration, TE_S, TE_D);
            bool isLong = durationCheck(duration, TE_L, TE_D);
            if (isShort || isLong) {
                bool b; if (manFeed(isShort, level, b)) { data=(data<<1)|b; bits++; }
                // Check preamble match at 16+ bits
                if (bits >= 64) { emitResult(data, bits, TE_S, "Honeywell"); state=Reset; }
            } else {
                if (bits >= 64) emitResult(data, bits, TE_S, "Honeywell");
                state = Reset;
            }
            break;
        }
        }
    }
};

// Alutech AT-4N (72 bit) — TE: 400/800, delta: 150
// Preamble: 4+ short alternating, then gap ~TE_S*10
class ProtoAlutechAt4n : public SubGhzDecoderBase {
    enum { Reset, CheckPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; uint64_t data_2 = 0;
    int bits = 0; int headerCount = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 400, TE_L = 800, TE_D = 150;
public:
    void reset() override { state=Reset; data=0; data_2=0; bits=0; headerCount=0; clearResult(); }
    const char* name() const override { return "Alutech AT-4N"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && durationCheck(duration, TE_S, TE_D)) { headerCount=1; state=CheckPre; }
            break;
        case CheckPre:
            if (durationCheck(duration, TE_S, TE_D)) headerCount++;
            else if (!level && DURATION_DIFF(duration, TE_S*10) < TE_D*10 && headerCount>=4) { data=0; data_2=0; bits=0; state=SaveDur; }
            else state=Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) {
                    if (bits<64) data=(data<<1)|1; else data_2=(data_2<<1)|1;
                    bits++; state=SaveDur;
                } else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) {
                    if (bits<64) data=(data<<1); else data_2=(data_2<<1);
                    bits++; state=SaveDur;
                } else if (duration > TE_S*3) {
                    if (bits>=72) { result={true,data,data_2,bits,TE_S,"Alutech AT-4N"}; }
                    state=Reset;
                } else state=Reset;
                if (bits>=72) { result={true,data,data_2,bits,TE_S,"Alutech AT-4N"}; state=Reset; }
            } else state=Reset;
            break;
        }
    }
};
