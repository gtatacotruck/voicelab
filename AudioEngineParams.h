#pragma once
#include "DSP/Common.h"
#include "DSP/Helpers.h"
#include "DSP/Reverb.h"
#include "DSP/ModFx.h"
#include "DSP/PitchShifter.h"
#include "DSP/Compressor.h"
#include "DSP/ThreeBandEq.h"
#include "DSP/Experimental.h"
#include "DSP/HeadSpacePanner.h"
#include "DSP/GlitchFx.h"
#include "DSP/VanillaFx.h"
#include "DSP/AutoTune.h"
#include "SoundboardRoutes.h"
#include "AudioEngineParams.h"
#include <deque>
#include <cmath>

namespace vlab
{
    class AudioEngine
    {
    public:
        AudioEngineParams params;
        std::atomic<bool> monitoring { false };

        // v1.9.6: where in the voice chain the soundboard is mixed in (a vlab::SoundboardInjectPoint). Default is the
        // original behaviour: after the voice FX. Deliberately NOT part of `params`, so "Reset All FX" and FX
        // presets never silently re-route the soundboard.
        std::atomic<int> sbInjectPoint { kSbAfterExperimental };
        dsp::AutoTune autoTune; // readouts (detected/target note) are atomics inside it, for the UI

        std::function<void (vlab::AudioBuffer&, int)> soundboardTtsMix;   // sb.get_audio_mix(include_sb=False, include_tts=True)
        std::function<void (vlab::AudioBuffer&, int)> soundboardMix;      // sb.get_audio_mix(include_sb=True,  include_tts=False)
        std::function<bool()>                          synthRunning;
        std::function<void (vlab::AudioBuffer&, int)>  synthProcess;
        std::function<void (vlab::AudioBuffer&, int)>  midiInstrumentMix;
        std::function<void (vlab::AudioBuffer&, int)>  modProcess; // wired to ModHost::process from Main.cpp
        std::function<void (vlab::AudioBuffer&, int)>  browserAudioMix; // wired to SystemAudioCapture::pull from Main.cpp

        void prepare (double sampleRateIn, int blockSize)
        {
            juce::ignoreUnused (sampleRateIn, blockSize); // engine is fixed at vlab::kSampleRate, same as the Python (SR=44100)
            cbTimes.clear();
        }

        void process (const vlab::AudioBuffer& in, vlab::AudioBuffer& out, int numSamples)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();

            out.makeCopyOf (in, true);
            if (out.getNumChannels() == 1)
            {
                out.setSize (2, numSamples, true, true, true);
                out.copyFrom (1, 0, out, 0, 0, numSamples);
            }

            // ── noise gate ──────────────────────────────────────────────
            const float gate = params.noiseGate.load();
            if (gate > 0.0f)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = out.getWritePointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float amp = std::abs (d[i]);
                        if (amp < gate)
                            d[i] *= amp / std::max (gate, 1e-9f);
                    }
                }
            }

            // ── autotune (mic only, so it runs before anything else is mixed in) ──
            if (params.autotuneEnabled.load())
            {
                if (! autotuneWasOn) { autoTune.reset(); autotuneWasOn = true; }
                dsp::AutoTune::Settings at;
                at.key = params.autotuneKey.load();
                at.scale = params.autotuneScale.load();
                at.retuneMs = params.autotuneRetuneMs.load();
                at.amount = params.autotuneAmount.load();
                at.refHz = params.autotuneRefHz.load();
                constexpr int kChunk = 1024;
                float mono[kChunk], tuned[kChunk];
                auto* l = out.getWritePointer (0);
                auto* r = out.getWritePointer (1);
                for (int base = 0; base < numSamples; base += kChunk)
                {
                    const int n = std::min (kChunk, numSamples - base);
                    for (int i = 0; i < n; ++i)
                        mono[i] = 0.5f * (l[base + i] + r[base + i]);
                    autoTune.process (mono, tuned, n, at);
                    for (int i = 0; i < n; ++i)
                        l[base + i] = r[base + i] = tuned[i];
                }
            }
            else if (autotuneWasOn)
            {
                autotuneWasOn = false;
                autoTune.reset();
            }

            // ── soundboard injection (see SoundboardRoutes.h) ──────────
            // The soundboard has to be pulled exactly once per block, so the route is read once and the mix happens
            // at whichever stage boundary below matches it.
            const int sbPoint = juce::jlimit (0, (int) kSbNumPoints - 1, sbInjectPoint.load());
            auto injectSoundboard = [&] (int point)
            {
                if (point != sbPoint || ! soundboardMix)
                    return;
                scratch.clear();
                soundboardMix (scratch, numSamples);
                if (params.sbReverb.load() > 0.0f)
                    sbSendReverb.process (scratch, params.sbReverb.load() * 0.5f, 0.5f, numSamples);
                const float sbVol = params.sbVol.load();
                for (int ch = 0; ch < 2; ++ch)
                    out.addFrom (ch, 0, scratch, ch, 0, numSamples, sbVol);
            };

            // ── TTS + synth mix-in ──────────────────────────────────────
            scratch.setSize (2, numSamples, false, true, true);
            if (soundboardTtsMix) { scratch.clear(); soundboardTtsMix (scratch, numSamples); addInto (out, scratch, numSamples); }
            if (synthRunning && synthRunning() && synthProcess) { scratch.clear(); synthProcess (scratch, numSamples); addInto (out, scratch, numSamples); }
            injectSoundboard (kSbStartOfChain);

            dsp::applyGain (out, params.gain.load(), numSamples);

            eq.process (out, params.eqLo.load(), params.eqMid.load(), params.eqHi.load(), numSamples);

            if (params.compRatio.load() > 1.0f)
                comp.process (out, params.compThreshold.load(), params.compRatio.load(),
                               params.compAttack.load(), params.compRelease.load(), numSamples);
            injectSoundboard (kSbAfterEqComp);

            pitchScratch.setSize (2, numSamples, false, true, true);
            shifter.process (out, pitchScratch, params.pitch.load(), numSamples);
            out.makeCopyOf (pitchScratch, true);
            if (params.vibratoDepth.load() > 0.0f)
                vibrato.process (out, params.vibratoRate.load(), params.vibratoDepth.load(), numSamples);
            injectSoundboard (kSbAfterPitch);

            const double budgetMs = (numSamples / vlab::kSampleRate) * 1000.0 * 0.8;

            if (params.distortion.load() > 0.0f) dsp::applyDistortion (out, params.distortion.load(), numSamples);
            if (params.bitcrush.load() > 0.0f)   dsp::applyBitcrush (out, params.bitcrush.load(), numSamples);
            if (params.tremoloDepth.load() > 0.0f) tremolo.process (out, params.tremoloRate.load(), params.tremoloDepth.load(), numSamples);
            injectSoundboard (kSbAfterDrive);

            stutter.process (out, params.stutterEnabled.load(), (dsp::StutterType) params.stutterType.load(),
                              params.stutterRate.load(), params.stutterHoldMs.load(), params.stutterRandomness.load(),
                              params.stutterMix.load(), numSamples);
            ringMod.process (out, params.ringModFreq.load(), params.ringModMix.load(), numSamples);
            escRepeat.process (out, params.escRepeatEnabled.load(), params.escRepeatHoldMs.load(),
                                params.escRepeatGrowth.load(), params.escRepeatMix.load(), numSamples);
            accRepeat.process (out, params.accRepeatEnabled.load(), params.accRepeatHoldMs.load(),
                                params.accRepeatSpeedGrowth.load(), params.accRepeatMix.load(), numSamples);
            decRepeat.process (out, params.decRepeatEnabled.load(), params.decRepeatHoldMs.load(),
                                params.decRepeatSpeedShrink.load(), params.decRepeatMix.load(), numSamples);
            pitchDownRepeat.process (out, params.pitchDownRepeatEnabled.load(), params.pitchDownRepeatHoldMs.load(),
                                      params.pitchDownRepeatSemitones.load(), params.pitchDownRepeatMix.load(), numSamples);
            scatter.process (out, params.scatterEnabled.load(), params.scatterGrainMs.load(), params.scatterMix.load(), numSamples);
            reverseBursts.process (out, params.reverseBurstEnabled.load(), params.reverseBurstProbability.load(),
                                    params.reverseBurstMs.load(), params.reverseBurstMix.load(), numSamples);
            glitchGate.process (out, params.glitchGateEnabled.load(), params.glitchGateStepMs.load(),
                                 params.glitchGateDensity.load(), params.glitchGateMix.load(), numSamples);
            bufferFreeze.process (out, params.freezeEnabled.load(), params.freezeGrainMs.load(), params.freezeMix.load(), numSamples);
            vinylStop.process (out, params.vinylStopEnabled.load(), params.vinylStopProbability.load(),
                                params.vinylStopMs.load(), params.vinylStopMix.load(), numSamples);
            brokenDelay.process (out, params.brokenDelayEnabled.load(), params.brokenDelayMs.load(),
                                  params.brokenDelayFeedback.load(), params.brokenDelayJumpRate.load(),
                                  params.brokenDelayMix.load(), numSamples);
            pitchJumps.process (out, params.pitchJumpEnabled.load(), params.pitchJumpRate.load(),
                                 params.pitchJumpMs.load(), params.pitchJumpSemitones.load(),
                                 params.pitchJumpMix.load(), numSamples);
            backmask.process (out, params.backmaskEnabled.load(), params.backmaskSegmentMs.load(),
                               params.backmaskMix.load(), numSamples);
            microFreeze.process (out, params.microFreezeEnabled.load(), params.microFreezeHoldMs.load(),
                                  params.microFreezeDensity.load(), params.microFreezeMix.load(), numSamples);
            bufferShuffle.process (out, params.bufferShuffleEnabled.load(), params.bufferShuffleHoldMs.load(),
                                    params.bufferShuffleChunks.load(), params.bufferShuffleMix.load(), numSamples);
            injectSoundboard (kSbAfterGlitch);

            if (elapsedMs (t0) < budgetMs * 0.6)
            {
                if (params.chorusMix.load() > 0.0f) chorus.process (out, params.chorusRate.load(), params.chorusDepth.load(), params.chorusMix.load(), numSamples);
                if (params.phaserMix.load() > 0.0f) phaser.process (out, params.phaserRate.load(), params.phaserDepth.load(), params.phaserMix.load(), numSamples);
                if (params.flangerMix.load() > 0.0f) flanger.process (out, params.flangerRate.load(), params.flangerDepth.load(), params.flangerFeedback.load(), params.flangerMix.load(), numSamples);
                if (params.autoWahMix.load() > 0.0f) autoWah.process (out, params.autoWahSensitivity.load(), params.autoWahRate.load(), params.autoWahDepth.load(), params.autoWahMix.load(), numSamples);
                if (params.telephoneMix.load() > 0.0f) telephone.process (out, params.telephoneMix.load(), numSamples);
                if (params.doublerMix.load() > 0.0f) doubler.process (out, params.doublerDetuneCents.load(), params.doublerDelayMs.load(), params.doublerMix.load(), numSamples);
            }
            if (elapsedMs (t0) < budgetMs * 0.75)
            {
                if (params.delayMix.load() > 0.0f)
                    delayFx.process (out, params.delayTime.load(), params.delay2Time.load(),
                                      params.delayFeedback.load(), params.delayMix.load(), numSamples);
            }
            injectSoundboard (kSbAfterModDelay);

            // ── pre-limiter ("loud mic bypass") ─────────────────────────
            if (params.loudMicBypass.load())
            {
                const float h = params.adaptiveHeadroom.load();
                const float th = params.limiterThreshold.load();
                dsp::applyGain (out, juce::Decibels::gainToDecibels (params.preLimiterGain.load()), numSamples);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = out.getWritePointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        d[i] = std::tanh (d[i] * 0.8f / (th + h)) * (th + h);
                }
                dsp::applyGain (out, juce::Decibels::gainToDecibels (params.postLimiterGain.load()), numSamples);
            }

            if (modProcess) modProcess (out, numSamples);
            // ── MIDI instrument mix ──────────────────────────────────────
            if (midiInstrumentMix) { scratch.clear(); midiInstrumentMix (scratch, numSamples); addInto (out, scratch, numSamples); }
            injectSoundboard (kSbAfterLimiterMods);

            // ── experimental fx block ────────────────────────────────────
            expFx.process (out, numSamples,
                params.expDeEsserEnabled.load(), params.expDeEsserAmount.load(),
                params.expAutoLevelEnabled.load(), params.expAutoLevelAmount.load(),
                params.expUnderwaterEnabled.load(), params.expUnderwaterAmount.load(),
                params.expPresenceEnabled.load(), params.expPresenceAmount.load(),
                params.expRadioSweepEnabled.load(), params.expRadioSweepRate.load(), params.expRadioSweepAmount.load(),
                params.expPumpEnabled.load(), params.expPumpRate.load(), params.expPumpDepth.load());

            // ── soundboard mix, Route 1 (with its own send-reverb, see injectSoundboard above) ──
            injectSoundboard (kSbAfterExperimental);

            if (browserAudioMix)
            {
                scratch.clear();
                browserAudioMix (scratch, numSamples);
                for (int ch = 0; ch < 2; ++ch)
                    out.addFrom (ch, 0, scratch, ch, 0, numSamples, params.browserAudioVol.load());
            }

            dsp::applyPan (out, params.pan.load(), numSamples);
            if (params.autoPanDepth.load() > 0.0f)
                autoPan.process (out, params.autoPanRate.load(), params.autoPanDepth.load(), numSamples);

            if (params.headSpaceEnabled.load())
                headSpace.process (out, params.headSpaceAzimuth.load(), params.headSpaceWidth.load(), numSamples);

            if (elapsedMs (t0) < budgetMs * 0.85 && params.reverbMix.load() > 0.0f)
                reverb.process (out, params.reverbMix.load(), params.reverbDecay.load(), numSamples);
            injectSoundboard (kSbBeforeMaster);

            dsp::applyGain (out, params.masterOutGain.load(), numSamples);

            if (params.loudMicBypass.load())
            {
                const float ceiling = params.limiterThreshold.load() - params.adaptiveHeadroom.load();
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = out.getWritePointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        d[i] = std::tanh (d[i] / std::max (0.001f, ceiling)) * ceiling;
                }
            }

            for (int ch = 0; ch < 2; ++ch)
                out.applyGainRamp (ch, 0, numSamples, 1.0f, 1.0f); // no-op ramp keeps buffer touched for denormal flush
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = out.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
            }

            vuPeakL = out.getMagnitude (0, 0, numSamples);
            vuPeakR = out.getMagnitude (1, 0, numSamples);
            visData.assign (out.getReadPointer (0), out.getReadPointer (0) + numSamples);
            visDataR.assign (out.getReadPointer (1), out.getReadPointer (1) + numSamples);

            const double elapsed = elapsedMs (t0);
            cpuPercent.store (juce::jlimit (0.0f, 100.0f, (float) (elapsed / std::max (0.001, budgetMs / 0.8) * 100.0)));
            cbTimes.push_back (elapsed);
            if (cbTimes.size() > 20) cbTimes.pop_front();
            const bool wasOverload = overload;
            if (cbTimes.size() == 20)
            {
                double sum5 = 0.0;
                for (size_t i = cbTimes.size() - 5; i < cbTimes.size(); ++i) sum5 += cbTimes[i];
                overload = (sum5 / 5.0) > budgetMs * 0.9;
            }
            if (overload != wasOverload && overloadCallback)
                overloadCallback (overload);
        }

        std::function<void (bool)> overloadCallback; // UI hook, mirrors self.engine._overload_cb
        float vuPeakL = 0.0f, vuPeakR = 0.0f;
        std::atomic<float> cpuPercent { 0.0f };
        std::vector<float> visData;
        std::vector<float> visDataR;

    private:
        dsp::Reverb reverb;
        dsp::Reverb sbSendReverb; // v1.9.6: the soundboard's send reverb used to share state with the master reverb above
        bool autotuneWasOn = false;
        dsp::Chorus chorus;
        dsp::Delay delayFx;
        dsp::Phaser phaser;
        dsp::PitchShifter shifter;
        dsp::Compressor comp;
        dsp::ThreeBandEq eq;
        dsp::Tremolo tremolo;
        dsp::ExperimentalFx expFx;
        dsp::HeadSpacePanner headSpace;
        dsp::Stutter stutter;
        dsp::RingModulator ringMod;
        dsp::EscalatingRepeat escRepeat;
        dsp::AcceleratingRepeat accRepeat;
        dsp::DecelerateRepeat decRepeat;
        dsp::RepeatPitchDown pitchDownRepeat;
        dsp::Scatter scatter;
        dsp::ReverseBursts reverseBursts;
        dsp::GlitchGate glitchGate;
        dsp::BufferFreeze bufferFreeze;
        dsp::VinylStop vinylStop;
        dsp::BrokenDelay brokenDelay;
        dsp::PitchJumps pitchJumps;
        dsp::Backmask backmask;
        dsp::MicroFreeze microFreeze;
        dsp::BufferShuffle bufferShuffle;
        dsp::Flanger flanger;
        dsp::AutoWah autoWah;
        dsp::Vibrato vibrato;
        dsp::AutoPan autoPan;
        dsp::Telephone telephone;
        dsp::Doubler doubler;

        vlab::AudioBuffer scratch, pitchScratch;
        std::deque<double> cbTimes;
        bool overload = false;

        static void addInto (vlab::AudioBuffer& dest, const vlab::AudioBuffer& src, int n)
        {
            for (int ch = 0; ch < 2; ++ch)
                dest.addFrom (ch, 0, src, ch, 0, n);
        }

        static double elapsedMs (juce::int64 t0)
        {
            const auto t1 = juce::Time::getHighResolutionTicks();
            return juce::Time::highResolutionTicksToSeconds (t1 - t0) * 1000.0;
        }
    };
}
