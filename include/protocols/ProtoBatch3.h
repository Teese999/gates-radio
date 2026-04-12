#pragma once
#include "SubGhzDecoderBase.h"

// Holtek HT12X (12 bit) — TE: 320/640, delta: 200
// Preamble: LOW ~8960us (28*TE_S)
class ProtoHoltekHT12X : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 320, TE_L = 640, TE_D = 200;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "Holtek HT12X"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*28) < 4000) { state=FoundStart; data=0; bits=0; }
            break;
        case FoundStart:
            if (!level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case SaveDur:
            if (!level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case CheckDur:
            if (level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (savedDur > 3200) { if (bits>=12) emitResult(data,bits,TE_S,"Holtek HT12X"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Linear Delta3 (8 bit) — TE: 500/2000, delta: 150
// Preamble: LOW ~35000us (70*TE_S)
class ProtoLinearDelta3 : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 500, TE_L = 2000, TE_D = 150;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "Linear Delta3"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*70) < TE_D*24) { state=SaveDur; data=0; bits=0; }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=8) emitResult(data,bits,TE_S,"Linear Delta3"); state=Reset; }
            break;
        case CheckDur:
            if (!level) {
                // Bit 0: HIGH=long(2000) + LOW=long(2000)
                if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                // Bit 1: HIGH=short(500) + LOW=3500us (7*TE_S)
                else if (durationCheck(savedDur, TE_S, TE_D) && DURATION_DIFF(duration, TE_S*7) < TE_D*3) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration > TE_S*10) { if (bits>=8) emitResult(data,bits,TE_S,"Linear Delta3"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Honeywell WDB (48 bit) — TE: 160/320, delta: 60
// Preamble: LOW ~480us (3*TE_S)
class ProtoHoneywellWDB : public SubGhzDecoderBase {
    enum { Reset, FoundStart, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 160, TE_L = 320, TE_D = 60;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "Honeywell WDB"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && durationCheck(duration, TE_S*3, TE_D)) { state=FoundStart; data=0; bits=0; }
            break;
        case FoundStart:
            if (level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; } else state=Reset;
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_S*3, TE_D)) { if (bits>=48) emitResult(data,bits,TE_S,"Honeywell WDB"); state=Reset; }
                else state=Reset;
                if (bits>=48) { emitResult(data,bits,TE_S,"Honeywell WDB"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// Security+ v2 (62 bit, Manchester) — TE: 250/500, delta: 110
// Preamble: LOW ~65000us (130*TE_L)
class ProtoSecPlusV2 : public SubGhzDecoderBase {
    enum { Reset, DecoderData } state = Reset;
    uint64_t data = 0; int bits = 0;
    int manState = 0; bool lastManBit = false;
    static constexpr unsigned long TE_S = 250, TE_L = 500, TE_D = 110;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState==0) { if (isShort) { manState=1; lastManBit=isHigh; return false; } else { outBit=isHigh; return true; } }
        else { if (isShort) { outBit=lastManBit; manState=0; return true; } else { manState=0; return false; } }
    }
public:
    void reset() override { state=Reset; data=0; bits=0; manState=0; clearResult(); }
    const char* name() const override { return "Security+ 2.0"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, 65000) < 11000) { state=DecoderData; data=0; bits=0; manState=0; }
            break;
        case DecoderData: {
            bool isS = durationCheck(duration, TE_S, TE_D);
            bool isL = durationCheck(duration, TE_L, TE_D);
            if (isS || isL) {
                bool b; if (manFeed(isS, level, b)) { data=(data<<1)|b; bits++; }
                if (bits>=62) { emitResult(data,bits,TE_S,"Security+ 2.0"); state=Reset; }
            } else {
                if (bits>=62) emitResult(data,bits,TE_S,"Security+ 2.0");
                state=Reset;
            }
            break;
        }
        }
    }
};

// Megacode (24 bit) — TE: 1000, delta: 200
// Preamble: LOW ~13000us (13*TE_S)
// Bit 0: HIGH=1000, gap=2000; Bit 1: HIGH=1000, gap=5000
class ProtoMegacode : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 1000, TE_D = 200;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "Megacode"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*13) < TE_D*17) { state=SaveDur; data=0; bits=0; }
            break;
        case SaveDur:
            if (level && durationCheck(duration, TE_S, TE_D)) { savedDur = duration; state=CheckDur; }
            else { if (bits>=24) emitResult(data,bits,TE_S,"Megacode"); state=Reset; }
            break;
        case CheckDur:
            if (!level) {
                if (DURATION_DIFF(duration, 2000) < 400) { data=(data<<1); bits++; state=SaveDur; }
                else if (DURATION_DIFF(duration, 5000) < 1000) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration >= 10000) { if (bits>=24) emitResult(data,bits,TE_S,"Megacode"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// iDo (48 bit) — TE: 450/1450, delta: 150
// Preamble: HIGH ~4500us (10*TE_S), LOW ~4500us (10*TE_S)
class ProtoIDo : public SubGhzDecoderBase {
    enum { Reset, FoundPre, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 450, TE_L = 1450, TE_D = 150;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "iDo"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && DURATION_DIFF(duration, TE_S*10) < TE_D*5) state=FoundPre;
            break;
        case FoundPre:
            if (!level && DURATION_DIFF(duration, TE_S*10) < TE_D*5) { data=0; bits=0; state=SaveDur; }
            else state=Reset;
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else state=Reset;
            break;
        case CheckDur:
            if (!level) {
                // Bit 0: short HIGH + long LOW
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                // Bit 1: short HIGH + short LOW (PWM)
                else if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (savedDur > 2400) { if (bits>=48) emitResult(data,bits,TE_S,"iDo"); state=Reset; }
                else state=Reset;
                if (bits>=48) { emitResult(data,bits,TE_S,"iDo"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// Mastercode (36 bit) — TE: 1072/2145, delta: 150
// Preamble: LOW ~16080us (15*TE_S)
class ProtoMastercode : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
    static constexpr unsigned long TE_S = 1072, TE_L = 2145, TE_D = 150;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "Mastercode"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (!level && DURATION_DIFF(duration, TE_S*15) < TE_D*15) { state=SaveDur; data=0; bits=0; }
            break;
        case SaveDur:
            if (level) { savedDur = duration; state = CheckDur; }
            else { if (bits>=36) emitResult(data,bits,TE_S,"Mastercode"); state=Reset; }
            break;
        case CheckDur:
            if (!level) {
                if (durationCheck(savedDur, TE_S, TE_D) && durationCheck(duration, TE_L, TE_D)) { data=(data<<1); bits++; state=SaveDur; }
                else if (durationCheck(savedDur, TE_L, TE_D) && durationCheck(duration, TE_S, TE_D)) { data=(data<<1)|1; bits++; state=SaveDur; }
                else if (duration > TE_S*10) { if (bits>=36) emitResult(data,bits,TE_S,"Mastercode"); state=Reset; }
                else state=Reset;
            } else state=Reset;
            break;
        }
    }
};

// Power Smart (64 bit, Manchester) — TE: 225/450, delta: 100
// No preamble — continuous Manchester, header pattern match
class ProtoPowerSmart : public SubGhzDecoderBase {
    enum { Reset, DecoderData } state = Reset;
    uint64_t data = 0; int bits = 0;
    int manState = 0; bool lastManBit = false;
    static constexpr unsigned long TE_S = 225, TE_L = 450, TE_D = 100;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState==0) { if (isShort) { manState=1; lastManBit=isHigh; return false; } else { outBit=isHigh; return true; } }
        else { if (isShort) { outBit=lastManBit; manState=0; return true; } else { manState=0; return false; } }
    }
public:
    void reset() override { state=Reset; data=0; bits=0; manState=0; clearResult(); }
    const char* name() const override { return "Power Smart"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        bool isS = durationCheck(duration, TE_S, TE_D);
        bool isL = durationCheck(duration, TE_L, TE_D);
        if (!isS && !isL) { state=Reset; return; }

        if (state == Reset) { data=0; bits=0; manState=0; state=DecoderData; }

        bool b;
        if (manFeed(isS, level, b)) {
            data = (data << 1) | b;
            bits++;
            // Check header pattern at 64 bits
            if (bits >= 64) {
                if ((data & 0xFF000000FF000000ULL) == 0xFD000000AA000000ULL) {
                    emitResult(data, bits, TE_S, "Power Smart");
                }
                state = Reset;
            }
        }
    }
};

// Somfy Keytis (80 bit, Manchester) — TE: 640/1280, delta: 250
// Same as Somfy Telis but 80 bits (56 + 24 extra)
class ProtoSomfyKeytis : public SubGhzDecoderBase {
    enum { Reset, FoundPre1, FoundPre2, DecoderData } state = Reset;
    uint64_t data = 0; uint64_t data_2 = 0;
    int bits = 0; int manState = 0; bool lastManBit = false; int headerCount = 0;
    static constexpr unsigned long TE_S = 640, TE_L = 1280, TE_D = 250;

    bool manFeed(bool isShort, bool isHigh, bool& outBit) {
        if (manState==0) { if (isShort) { manState=1; lastManBit=isHigh; return false; } else { outBit=isHigh; return true; } }
        else { if (isShort) { outBit=lastManBit; manState=0; return true; } else { manState=0; return false; } }
    }
public:
    void reset() override { state=Reset; data=0; data_2=0; bits=0; manState=0; headerCount=0; clearResult(); }
    const char* name() const override { return "Somfy Keytis"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (DURATION_DIFF(duration, TE_S*4) < 1000) { headerCount=1; state=FoundPre1; }
            break;
        case FoundPre1:
            if (DURATION_DIFF(duration, TE_S*4) < 1000) { headerCount++; if (headerCount>1) state=FoundPre2; }
            else state=Reset;
            break;
        case FoundPre2:
            if (level && DURATION_DIFF(duration, TE_S*7.6) < 1000) { data=0; data_2=0; bits=0; manState=0; state=DecoderData; }
            else if (DURATION_DIFF(duration, TE_S*4) < 1000) { /* still preamble */ }
            else state=Reset;
            break;
        case DecoderData: {
            bool isS = durationCheck(duration, TE_S, TE_D);
            bool isL = durationCheck(duration, TE_L, TE_D);
            if (isS || isL) {
                bool b; if (manFeed(isS, level, b)) {
                    if (bits<56) data=(data<<1)|b; else data_2=(data_2<<1)|b;
                    bits++;
                }
                if (bits>=80) { result={true,data,data_2,bits,TE_S,"Somfy Keytis"}; state=Reset; }
            } else {
                if (bits>=80) { result={true,data,data_2,bits,TE_S,"Somfy Keytis"}; }
                state=Reset;
            }
            break;
        }
        }
    }
};
