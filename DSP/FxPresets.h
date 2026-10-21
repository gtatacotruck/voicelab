#pragma once
#include "AudioEngineParams.h"
#include <juce_core/juce_core.h>

namespace vlab
{
    #define VLAB_FX_FLOAT_FIELDS(X) \
        X(gain) X(pitch) X(pan) X(masterOutGain) X(noiseGate) \
        X(distortion) \
        X(chorusRate) X(chorusDepth) X(chorusMix) \
        X(phaserRate) X(phaserDepth) X(phaserMix) \
        X(tremoloRate) X(tremoloDepth) \
        X(delayTime) X(delay2Time) X(delayFeedback) X(delayMix) \
        X(reverbMix) X(reverbDecay) \
        X(compThreshold) X(compRatio) X(compAttack) X(compRelease) \
        X(eqLo) X(eqMid) X(eqHi) X(bitcrush) \
        X(limiterThreshold) X(preLimiterGain) X(postLimiterGain) X(adaptiveHeadroom) \
        X(midiVol) \
        X(headSpaceAzimuth) X(headSpaceWidth) \
        X(expDeEsserAmount) X(expAutoLevelAmount) X(expUnderwaterAmount) \
        X(expPresenceAmount) X(expRadioSweepRate) X(expRadioSweepAmount) \
        X(expPumpRate) X(expPumpDepth) \
        X(stutterRate) X(stutterHoldMs) X(stutterRandomness) X(stutterMix) \
        X(ringModFreq) X(ringModMix) \
        X(escRepeatHoldMs) X(escRepeatGrowth) X(escRepeatMix) \
        X(accRepeatHoldMs) X(accRepeatSpeedGrowth) X(accRepeatMix) \
        X(decRepeatHoldMs) X(decRepeatSpeedShrink) X(decRepeatMix) \
        X(pitchDownRepeatHoldMs) X(pitchDownRepeatSemitones) X(pitchDownRepeatMix) \
        X(scatterGrainMs) X(scatterMix) \
        X(reverseBurstProbability) X(reverseBurstMs) X(reverseBurstMix) \
        X(glitchGateStepMs) X(glitchGateDensity) X(glitchGateMix) \
        X(freezeGrainMs) X(freezeMix) \
        X(vinylStopProbability) X(vinylStopMs) X(vinylStopMix) \
        X(brokenDelayMs) X(brokenDelayFeedback) X(brokenDelayJumpRate) X(brokenDelayMix) \
        X(pitchJumpRate) X(pitchJumpMs) X(pitchJumpSemitones) X(pitchJumpMix) \
        X(backmaskSegmentMs) X(backmaskMix) \
        X(microFreezeHoldMs) X(microFreezeDensity) X(microFreezeMix) \
        X(bufferShuffleHoldMs) X(bufferShuffleMix) \
        X(flangerRate) X(flangerDepth) X(flangerFeedback) X(flangerMix) \
        X(autoWahSensitivity) X(autoWahRate) X(autoWahDepth) X(autoWahMix) \
        X(vibratoRate) X(vibratoDepth) \
        X(autoPanRate) X(autoPanDepth) \
        X(telephoneMix) \
        X(doublerDetuneCents) X(doublerDelayMs) X(doublerMix) \
        X(browserAudioVol) \
        X(autotuneRetuneMs) X(autotuneAmount) X(autotuneRefHz)

    #define VLAB_FX_BOOL_FIELDS(X) \
        X(loudMicBypass) X(headSpaceEnabled) \
        X(expDeEsserEnabled) X(expAutoLevelEnabled) X(expUnderwaterEnabled) \
        X(expPresenceEnabled) X(expRadioSweepEnabled) X(expPumpEnabled) \
        X(stutterEnabled) \
        X(escRepeatEnabled) X(accRepeatEnabled) X(decRepeatEnabled) X(pitchDownRepeatEnabled) X(scatterEnabled) \
        X(reverseBurstEnabled) X(glitchGateEnabled) X(freezeEnabled) X(vinylStopEnabled) \
        X(brokenDelayEnabled) X(pitchJumpEnabled) X(backmaskEnabled) X(microFreezeEnabled) X(bufferShuffleEnabled) \
        X(autotuneEnabled)

    #define VLAB_FX_INT_FIELDS(X) \
        X(stutterType) X(bufferShuffleChunks) X(autotuneKey) X(autotuneScale)

    inline juce::var fxParamsToVar (const AudioEngineParams& p)
    {
        auto* o = new juce::DynamicObject();
        #define VLAB_FX_WRITE_F(name) o->setProperty (#name, (double) p.name.load());
        #define VLAB_FX_WRITE_B(name) o->setProperty (#name, (bool) p.name.load());
        #define VLAB_FX_WRITE_I(name) o->setProperty (#name, (int) p.name.load());
        VLAB_FX_FLOAT_FIELDS (VLAB_FX_WRITE_F)
        VLAB_FX_BOOL_FIELDS (VLAB_FX_WRITE_B)
        VLAB_FX_INT_FIELDS (VLAB_FX_WRITE_I)
        #undef VLAB_FX_WRITE_F
        #undef VLAB_FX_WRITE_B
        #undef VLAB_FX_WRITE_I
        return juce::var (o);
    }

    inline void fxParamsFromVar (AudioEngineParams& p, const juce::var& v)
    {
        if (! v.isObject())
            return;
        #define VLAB_FX_READ_F(name) p.name = (float) (double) v.getProperty (#name, (double) p.name.load());
        #define VLAB_FX_READ_B(name) p.name = (bool) v.getProperty (#name, (bool) p.name.load());
        #define VLAB_FX_READ_I(name) p.name = (int) v.getProperty (#name, (int) p.name.load());
        VLAB_FX_FLOAT_FIELDS (VLAB_FX_READ_F)
        VLAB_FX_BOOL_FIELDS (VLAB_FX_READ_B)
        VLAB_FX_INT_FIELDS (VLAB_FX_READ_I)
        #undef VLAB_FX_READ_F
        #undef VLAB_FX_READ_B
        #undef VLAB_FX_READ_I
    }

    #undef VLAB_FX_FLOAT_FIELDS
    #undef VLAB_FX_BOOL_FIELDS
    #undef VLAB_FX_INT_FIELDS

    // Shape of the "fxPresets" save section: { "My Preset Name": { ...fxParamsToVar()... }, ... }
    inline juce::DynamicObject::Ptr loadFxPresetsFile (const juce::File& f)
    {
        if (f.existsAsFile())
        {
            auto parsed = juce::JSON::parse (f);
            if (auto* obj = parsed.getDynamicObject())
                return obj;
        }
        return new juce::DynamicObject();
    }

    inline void saveFxPresetsFile (const juce::File& f, const juce::DynamicObject::Ptr& presets)
    {
        f.replaceWithText (juce::JSON::toString (juce::var (presets.get())));
    }
}
