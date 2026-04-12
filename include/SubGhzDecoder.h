#ifndef SUBGHZ_DECODER_H
#define SUBGHZ_DECODER_H

// ============================================================================
// SubGhz Multi-Decoder — ported from Flipper Zero Unleashed firmware
// 30+ protocol decoders run in parallel, first match wins.
// ============================================================================

#include "SubGhzDecoderBase.h"
#include "protocols/ProtoCAME.h"
#include "protocols/ProtoNiceFlo.h"
#include "protocols/ProtoPrinceton.h"
#include "protocols/ProtoNeroRadio.h"
#include "protocols/ProtoKeeloq.h"
#include "protocols/ProtoGateTx.h"
#include "protocols/ProtoStarLine.h"
#include "protocols/ProtoBatch2.h"
#include "protocols/ProtoBatch3.h"

// Generic OOK fallback — catches any 1:2..1:4 ratio signal
class ProtoGenericOOK : public SubGhzDecoderBase {
    enum { Reset, SaveDur, CheckDur } state = Reset;
    uint64_t data = 0; int bits = 0; unsigned long savedDur = 0;
public:
    void reset() override { state=Reset; data=0; bits=0; clearResult(); }
    const char* name() const override { return "GenericOOK"; }
    SubGhzDecoderResult getResult() const override { return result; }
    void feed(bool level, unsigned long duration) override {
        switch (state) {
        case Reset:
            if (level && duration >= 150 && duration <= 2000) { savedDur=duration; state=CheckDur; data=0; bits=0; }
            break;
        case SaveDur:
            if (level) { savedDur=duration; state=CheckDur; }
            else { if (bits>=12) emitResult(data,bits,0,"OOK"); state=Reset; }
            break;
        case CheckDur:
            if (!level) {
                float ratio = (savedDur>duration) ? (float)savedDur/duration : (float)duration/savedDur;
                if (ratio>=2.0f && ratio<=6.0f) {
                    data=(data<<1)|(savedDur>duration?1:0); bits++; state=SaveDur;
                    if (bits>=64) { emitResult(data,bits,0,"OOK"); state=Reset; }
                } else if (duration>2000 && bits>=12) { emitResult(data,bits,0,"OOK"); state=Reset; }
                else { if (bits>=12) emitResult(data,bits,0,"OOK"); state=Reset; }
            } else state=Reset;
            break;
        }
    }
};

// ============================================================================
// Main multi-decoder: 30 protocols + GenericOOK fallback
// ============================================================================
class SubGhzMultiDecoder {
    static constexpr int MAX = 48;
    SubGhzMultiDecoderT<MAX> decoder;

    // All protocol instances (stack allocated — no heap)
    // Batch 1: Core protocols
    ProtoCAME came;
    ProtoCameTwee cameTwee;
    ProtoCameAtomo cameAtomo;
    ProtoNiceFlo niceFlo;
    ProtoNiceFlorS niceFlorS;
    ProtoPrinceton princeton;
    ProtoNeroRadio neroRadio;
    ProtoNeroSketch neroSketch;
    ProtoKeeloq keeloq;
    ProtoGateTx gateTx;
    ProtoHoltek holtek;
    ProtoLinear linear;
    ProtoChamberlain chamberlain;
    ProtoHormann hormann;
    ProtoFaacSlh faacSlh;
    ProtoStarLine starLine;
    ProtoSomfyTelis somfyTelis;
    ProtoBftMitto bftMitto;
    ProtoDooya dooya;
    ProtoMarantec marantec;
    // Batch 2
    ProtoClemsa clemsa;
    ProtoDoitrand doitrand;
    ProtoPhoenixV2 phoenixV2;
    ProtoMagellan magellan;
    ProtoLegrand legrand;
    ProtoKingGates kingGates;
    ProtoAnsonic ansonic;
    ProtoSMC5326 smc5326;
    ProtoHoneywell honeywell;
    ProtoAlutechAt4n alutechAt4n;
    // Batch 3
    ProtoHoltekHT12X holtekHT12X;
    ProtoLinearDelta3 linearDelta3;
    ProtoHoneywellWDB honeywellWDB;
    ProtoSecPlusV2 secPlusV2;
    ProtoMegacode megacode;
    ProtoIDo ido;
    ProtoMastercode mastercode;
    ProtoPowerSmart powerSmart;
    ProtoSomfyKeytis somfyKeytis;
    // Fallback
    ProtoGenericOOK genericOok;

public:
    SubGhzMultiDecoder() {
        // Preamble-based decoders first, sorted by uniqueness
        decoder.add(&came);
        decoder.add(&cameTwee);
        decoder.add(&cameAtomo);
        decoder.add(&niceFlo);
        decoder.add(&niceFlorS);
        decoder.add(&neroRadio);
        decoder.add(&neroSketch);
        decoder.add(&keeloq);
        decoder.add(&starLine);
        decoder.add(&alutechAt4n);
        decoder.add(&bftMitto);
        decoder.add(&faacSlh);
        decoder.add(&kingGates);
        decoder.add(&magellan);
        decoder.add(&honeywell);
        decoder.add(&honeywellWDB);
        decoder.add(&somfyTelis);
        decoder.add(&somfyKeytis);
        decoder.add(&secPlusV2);
        decoder.add(&hormann);
        decoder.add(&chamberlain);
        decoder.add(&dooya);
        decoder.add(&phoenixV2);
        decoder.add(&doitrand);
        decoder.add(&clemsa);
        decoder.add(&marantec);
        decoder.add(&megacode);
        decoder.add(&ido);
        decoder.add(&mastercode);
        decoder.add(&powerSmart);
        decoder.add(&legrand);
        decoder.add(&ansonic);
        decoder.add(&smc5326);
        decoder.add(&holtekHT12X);
        decoder.add(&linearDelta3);
        decoder.add(&princeton);
        decoder.add(&gateTx);
        decoder.add(&holtek);
        decoder.add(&linear);
        decoder.add(&genericOok); // Always last
    }

    void resetAll() { decoder.resetAll(); }

    SubGhzDecoderResult feed(bool level, unsigned long duration) {
        return decoder.feed(level, duration);
    }

    int getProtocolCount() const { return decoder.getCount(); }
};

using DecoderResult = SubGhzDecoderResult;

#endif // SUBGHZ_DECODER_H
