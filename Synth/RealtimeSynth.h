#pragma once
#include "../DSP/Common.h"
#include "SynthParams.h"
#include "Song.h"
#include "NoteUtils.h"
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace vlab
{
    class RealtimeSynth
    {
    public:
        static constexpr int kUnisonMax = 7;
        static constexpr int kMaxPoolVoices = 8;
        std::atomic<bool> running { false };

        std::vector<SongStep> songSteps;
        int   songStepIdx = 0;
        double songTimeAcc = 0.0;
        double songNoteFreq = 0.0;
        float songNoteVel = 1.0f;
        bool  songNoteOn = false;
        bool  songLoop = true;
        bool  songActive = false;

        void loadSong (const Song& song)
        {
            songSteps = song.steps;
            songLoop = song.loop;
            songStepIdx = 0;
            songTimeAcc = 0.0;
            songNoteOn = false;
            songActive = ! songSteps.empty();
        }

        void noteOn()  { legacyVoice.envStage = 1; legacyVoice.envTime = 0.0; legacyVoice.velocity = 1.0f; running = true; }
        void noteOff()
        {
            if (legacyVoice.envStage != 0)
            {
                legacyVoice.releaseLevel = legacyVoice.lastEnvValue;
                legacyVoice.envStage = 4;
                legacyVoice.envTime = 0.0;
            }
        }

        void noteOn (int id, float freqHz, float velocity = 1.0f)
        {
            SynthVoice* v = findVoiceById (id);
            if (v == nullptr) v = allocateVoice();
            v->active = true;
            v->noteId = id;
            v->velocity = juce::jlimit (0.0f, 1.0f, velocity);
            v->targetFreq = freqHz;
            if (v->curFreq <= 0.0f) v->curFreq = freqHz; // don't glide in from silence on a fresh voice
            v->envStage = 1;
            v->envTime = 0.0;
            v->ageCounter = ++voiceAgeCounter;
            running = true;
        }

        void noteOff (int id)
        {
            auto* v = findVoiceById (id);
            if (v != nullptr && v->envStage != 0)
            {
                v->releaseLevel = v->lastEnvValue;
                v->envStage = 4;
                v->envTime = 0.0;
            }
        }

        void allNotesOff()
        {
            noteOff();
            for (auto& v : poolVoices)
                if (v.active && v.envStage != 0)
                {
                    v.releaseLevel = v.lastEnvValue;
                    v.envStage = 4;
                    v.envTime = 0.0;
                }
        }

        // out must be pre-sized to (2, frames).
        void process (vlab::AudioBuffer& out, int frames, const SynthParams& p)
        {
            out.clear();
            if (! running.load())
                return;

            const double dt = frames / vlab::kSampleRate;
            mixBuffer.assign ((size_t) frames, 0.0f);
            voiceScratch.assign ((size_t) frames, 0.0f);

            const float lfoRate = p.modLfoRate.load(), lfoDepth = p.modLfoDepth.load();
            const double nowSecs = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            const float lfo = lfoAt (p.modLfoShape.load(), lfoRate, nowSecs) * lfoDepth;
            const auto modTarget = p.modTarget.load();

            bool anyActive = false;

            {
                float freq = legacyVoice.curFreq;
                const float targetFreq = p.freq.load() * std::pow (2.0f, p.detune.load() / 1200.0f);
                const float porta = p.portamento.load();
                if (porta > 0.0f && legacyVoice.curFreq != targetFreq)
                {
                    const float k = std::exp (-1.0f / std::max (1e-4f, porta * (float) vlab::kSampleRate / frames));
                    legacyVoice.curFreq = legacyVoice.curFreq * k + targetFreq * (1.0f - k);
                }
                else
                {
                    legacyVoice.curFreq = targetFreq;
                }
                freq = legacyVoice.curFreq;

                if (songActive && ! songSteps.empty())
                {
                    advanceSong (frames, p);
                    freq = songNoteOn ? (float) songNoteFreq : 0.0f;
                    legacyVoice.velocity = songNoteVel;
                }
                else if (p.arpOn.load())
                {
                    freq *= std::pow (2.0f, p.arpOffsetSemitones.load() / 12.0f);
                }

                if (legacyVoice.envStage != 0 && freq > 0.0f)
                {
                    renderVoice (legacyVoice, frames, p, freq, lfo, modTarget, dt);
                    anyActive = true;
                }
                else if (legacyVoice.envStage != 0)
                {
                    std::fill (voiceScratch.begin(), voiceScratch.end(), 0.0f);
                    envelope (legacyVoice, p, dt);
                }
            }

            // Polyphonic pool voices.
            for (auto& v : poolVoices)
            {
                if (! v.active) continue;

                const float porta = p.portamento.load();
                if (porta > 0.0f && v.curFreq != v.targetFreq)
                {
                    const float k = std::exp (-1.0f / std::max (1e-4f, porta * (float) vlab::kSampleRate / frames));
                    v.curFreq = v.curFreq * k + v.targetFreq * (1.0f - k);
                }
                else
                {
                    v.curFreq = v.targetFreq;
                }

                renderVoice (v, frames, p, v.curFreq, lfo, modTarget, dt);
                anyActive = true;

                if (v.envStage == 0)
                    v.active = false; // release finished, free the slot
            }

            running.store (anyActive);

            // ── Bus FX: distortion, bitcrush, chorus, delay ────────────────
            const float dist = p.fxDist.load();
            if (dist > 0.0f)
                for (auto& s : mixBuffer) s = std::tanh (s * (1.0f + dist)) / (1.0f + dist * 0.1f);

            const float bits = p.fxBitcrush.load();
            if (bits > 0.0f)
            {
                const float steps = std::pow (2.0f, std::max (1, (int) bits));
                for (auto& s : mixBuffer) s = std::round (s * steps) / steps;
            }

            const float chorusMix = p.chorusMix.load();
            if (chorusMix > 0.0f)
                applyChorus (mixBuffer, frames, p);

            const float delayMix = p.delayMix.load();
            if (delayMix > 0.0f)
                applyDelay (mixBuffer, frames, p);

            const float masterVol = p.masterVol.load();
            for (int i = 0; i < frames; ++i)
            {
                const float v = mixBuffer[(size_t) i] * masterVol;
                out.setSample (0, i, v);
                out.setSample (1, i, v);
            }
            dsp_applyPan (out, p.modPan.load(), frames);
        }

    private:
        struct SynthVoice
        {
            bool active = false;
            int noteId = -1;
            float velocity = 1.0f;
            float curFreq = 0.0f, targetFreq = 0.0f;
            std::array<float, kUnisonMax> phases {};
            float phase2 = 0.0f;
            int envStage = 0; // 0 idle,1 A,2 D,3 S,4 R
            double envTime = 0.0;
            float releaseLevel = 0.0f;
            float lastEnvValue = 0.0f;
            float svfLow = 0.0f, svfBand = 0.0f; // per-voice filter state
            juce::uint32 ageCounter = 0;
        };

        SynthVoice legacyVoice { false, -1, 1.0f, 440.0f, 440.0f };
        std::array<SynthVoice, kMaxPoolVoices> poolVoices;
        juce::uint32 voiceAgeCounter = 0;

        std::vector<float> mixBuffer, voiceScratch;
        std::mt19937 rng { std::random_device{}() };

        // Chorus: short modulated delay line.
        std::vector<float> chorusBuf;
        int chorusWritePos = 0;
        double chorusPhase = 0.0;

        // Delay: longer feedback delay line.
        std::vector<float> delayBuf;
        int delayWritePos = 0;

        SynthVoice* findVoiceById (int id)
        {
            for (auto& v : poolVoices)
                if (v.active && v.noteId == id)
                    return &v;
            return nullptr;
        }

        SynthVoice* allocateVoice()
        {
            for (auto& v : poolVoices)
                if (! v.active)
                    return &v;

            // All voices busy: steal the oldest one (lowest ageCounter).
            SynthVoice* oldest = &poolVoices[0];
            for (auto& v : poolVoices)
                if (v.ageCounter < oldest->ageCounter)
                    oldest = &v;
            return oldest;
        }

        static float waveAt (float phase, WaveType w)
        {
            const float twoPi = 2.0f * juce::MathConstants<float>::pi;
            auto sawAt = [twoPi] (float ph) { return 2.0f * (ph / twoPi - std::floor (ph / twoPi + 0.5f)); };
            switch (w)
            {
                case WaveType::Sine:     return std::sin (phase);
                case WaveType::Square:   return (std::sin (phase) >= 0.0f) ? 1.0f : -1.0f;
                case WaveType::Saw:      return sawAt (phase);
                case WaveType::Triangle: return 2.0f * std::abs (2.0f * (phase / twoPi - std::floor (phase / twoPi + 0.5f))) - 1.0f;
                case WaveType::Pulse:    return (std::fmod (phase, twoPi) / twoPi < 0.3f) ? 1.0f : -1.0f;
                case WaveType::SuperSaw:
                    return (sawAt (phase * 0.99f) + sawAt (phase) + sawAt (phase * 1.01f)) / 3.0f;
                case WaveType::Organ:
                {
                    return 0.5f * std::sin (phase) + 0.25f * std::sin (phase * 2.0f)
                         + 0.15f * std::sin (phase * 3.0f) + 0.1f * std::sin (phase * 4.0f);
                }
                case WaveType::Wavefolder:
                {
                    float s = std::sin (phase) * 2.2f;
                    while (s > 1.0f || s < -1.0f)
                        s = (s > 1.0f) ? (2.0f - s) : (s < -1.0f ? (-2.0f - s) : s);
                    return s;
                }
            }
            return std::sin (phase);
        }

        static float lfoAt (LfoShape shape, float rateHz, double nowSecs)
        {
            const float ph = std::fmod ((float) (rateHz * nowSecs), 1.0f);
            switch (shape)
            {
                case LfoShape::Sine:     return std::sin (2.0f * juce::MathConstants<float>::pi * ph);
                case LfoShape::Triangle: return 4.0f * std::abs (ph - 0.5f) - 1.0f;
                case LfoShape::Square:   return ph < 0.5f ? 1.0f : -1.0f;
                case LfoShape::SampleHold:
                {
                    const juce::int64 cycle = (juce::int64) std::floor (rateHz * nowSecs);
                    juce::uint32 h = (juce::uint32) (cycle * 2654435761u);
                    h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16;
                    return (float) (h % 20000) / 10000.0f - 1.0f;
                }
            }
            return std::sin (2.0f * juce::MathConstants<float>::pi * ph);
        }

        void renderVoice (SynthVoice& v, int frames, const SynthParams& p, float freq,
                           float lfo, ModTarget modTarget, double dt)
        {
            if (modTarget == ModTarget::Pitch)
                freq *= 1.0f + lfo * 0.1f;

            const int uCount = juce::jmax (1, p.unison.load());
            const float uDetune = p.unisonDetune.load();
            std::array<float, kUnisonMax> offsets {};
            if (uCount > 1)
                for (int i = 0; i < uCount; ++i)
                    offsets[(size_t) i] = -uDetune / 2.0f + uDetune * i / (float) (uCount - 1);
            else
                offsets[0] = 0.0f;

            const WaveType wave = p.wave.load();
            std::fill (voiceScratch.begin(), voiceScratch.end(), 0.0f);

            for (int ui = 0; ui < uCount; ++ui)
            {
                const float f = (freq > 0.0f) ? freq * std::pow (2.0f, offsets[(size_t) ui] / 1200.0f) : 0.0f;
                const float pinc = (f > 0.0f) ? 2.0f * juce::MathConstants<float>::pi * f / (float) vlab::kSampleRate : 0.0f;

                for (int i = 0; i < frames; ++i)
                {
                    float ph = v.phases[(size_t) ui] + i * pinc;

                    const float fmDepth = p.fmDepth.load();
                    if (fmDepth > 0.0f)
                    {
                        const float fmInc = 2.0f * juce::MathConstants<float>::pi * p.fmFreq.load() / (float) vlab::kSampleRate;
                        const float fmPh = v.phases[(size_t) ui] + i * fmInc;
                        ph += std::sin (fmPh) * fmDepth;
                    }
                    voiceScratch[(size_t) i] += waveAt (ph, wave);
                }
                v.phases[(size_t) ui] = std::fmod (v.phases[(size_t) ui] + frames * pinc, 2.0f * juce::MathConstants<float>::pi);
            }
            for (auto& s : voiceScratch) s /= (float) uCount;

            // OSC2, with its own mod-LFO target (mix amount wobble).
            float osc2Mix = p.osc2Mix.load();
            if (modTarget == ModTarget::Osc2Mix)
                osc2Mix = juce::jlimit (0.0f, 1.0f, osc2Mix + lfo * 0.5f);
            if (osc2Mix > 0.0f)
            {
                const float f2 = freq * std::pow (2.0f, p.osc2Detune.load() / 1200.0f);
                const float inc2 = 2.0f * juce::MathConstants<float>::pi * f2 / (float) vlab::kSampleRate;
                const WaveType osc2Wave = p.osc2Wave.load();
                for (int i = 0; i < frames; ++i)
                {
                    const float ph2 = v.phase2 + i * inc2;
                    voiceScratch[(size_t) i] = voiceScratch[(size_t) i] * (1.0f - osc2Mix) + waveAt (ph2, osc2Wave) * osc2Mix;
                }
                v.phase2 = std::fmod (v.phase2 + frames * inc2, 2.0f * juce::MathConstants<float>::pi);
            }

            const float subMix = p.subMix.load();
            if (subMix > 0.0f)
            {
                for (int i = 0; i < frames; ++i)
                {
                    const float subPh = v.phases[0] * 0.5f + i * juce::MathConstants<float>::pi * freq / (float) vlab::kSampleRate;
                    voiceScratch[(size_t) i] = voiceScratch[(size_t) i] * (1.0f - subMix) + std::sin (subPh) * subMix;
                }
            }
            const float noiseMix = p.noiseMix.load();
            if (noiseMix > 0.0f)
            {
                std::uniform_real_distribution<float> ndist (-1.0f, 1.0f);
                for (int i = 0; i < frames; ++i)
                    voiceScratch[(size_t) i] = voiceScratch[(size_t) i] * (1.0f - noiseMix) + ndist (rng) * noiseMix;
            }

            // Amp envelope, scaled by note velocity per velocitySensitivity.
            const float env = envelope (v, p, dt);
            v.lastEnvValue = env;
            const float velAmt = p.velocitySensitivity.load();
            const float velScale = 1.0f - velAmt + velAmt * v.velocity;
            for (auto& s : voiceScratch) s *= env * velScale;
            if (modTarget == ModTarget::Amp)
                for (auto& s : voiceScratch) s *= (1.0f + lfo);

            // Multi-mode filter with resonance and its own envelope amount.
            const float fcut = p.filterCutoff.load();
            const float filterEnvAmt = p.filterEnvAmount.load();
            const auto filterType = p.filterType.load();
            if (fcut < 1.0f || filterType != FilterType::LowPass || filterEnvAmt != 0.0f || modTarget == ModTarget::Filter)
            {
                float fcBase = std::max (0.001f, fcut) * 0.499f;
                if (modTarget == ModTarget::Filter)
                    fcBase = juce::jlimit (0.0005f, 0.499f, fcBase + lfo * 0.2f);
                const float fcMod = juce::jlimit (0.0005f, 0.499f, fcBase + filterEnvAmt * env * 0.499f);
                const float fCoef = 2.0f * std::sin (juce::MathConstants<float>::pi * fcMod);
                const float q = juce::jlimit (0.08f, 2.0f, 2.0f - p.filterResonance.load() * 1.85f);

                for (int i = 0; i < frames; ++i)
                {
                    const float input = voiceScratch[(size_t) i];
                    v.svfLow += fCoef * v.svfBand;
                    const float high = input - v.svfLow - q * v.svfBand;
                    v.svfBand += fCoef * high;
                    const float notch = high + v.svfLow;

                    switch (filterType)
                    {
                        case FilterType::LowPass:  voiceScratch[(size_t) i] = v.svfLow; break;
                        case FilterType::HighPass: voiceScratch[(size_t) i] = high; break;
                        case FilterType::BandPass: voiceScratch[(size_t) i] = v.svfBand; break;
                        case FilterType::Notch:    voiceScratch[(size_t) i] = notch; break;
                    }
                }
            }

            const float drive = p.drive.load();
            if (drive > 0.0f)
                for (auto& s : voiceScratch) s = std::tanh (s * (1.0f + drive * 3.0f)) / std::tanh (1.0f + drive * 3.0f);

            for (int i = 0; i < frames; ++i)
                mixBuffer[(size_t) i] += voiceScratch[(size_t) i];
        }

        float envelope (SynthVoice& v, const SynthParams& p, double dt)
        {
            const float a = p.envA.load(), d = p.envD.load(), s = p.envS.load(), r = p.envR.load();
            if (v.envStage == 0) return 0.0f;
            v.envTime += dt;
            if (v.envStage == 1)
            {
                if (v.envTime >= a) { v.envStage = 2; v.envTime = 0.0; return 1.0f; }
                return (float) (v.envTime / std::max ((double) a, 1e-5));
            }
            if (v.envStage == 2)
            {
                if (v.envTime >= d) { v.envStage = 3; v.envTime = 0.0; return s; }
                return 1.0f - (float) (v.envTime / std::max ((double) d, 1e-5)) * (1.0f - s);
            }
            if (v.envStage == 3) return s;
            if (v.envStage == 4)
            {
                const float lvl = v.releaseLevel * std::max (0.0f, 1.0f - (float) (v.envTime / std::max ((double) r, 1e-5)));
                if (v.envTime >= r) v.envStage = 0;
                return lvl;
            }
            return 0.0f;
        }

        void advanceSong (int frames, const SynthParams& p)
        {
            if (songSteps.empty()) return;
            const double beatSecs = 60.0 / p.arpBpm.load();
            const double dt = frames / vlab::kSampleRate;
            songTimeAcc += dt;

            SongStep* step = &songSteps[(size_t) (songStepIdx % (int) songSteps.size())];
            double stepDur = step->dur * beatSecs;

            if (songTimeAcc >= stepDur)
            {
                songTimeAcc -= stepDur;
                ++songStepIdx;
                if (songStepIdx >= (int) songSteps.size())
                {
                    if (songLoop) songStepIdx = 0;
                    else { songActive = false; songNoteVel = 0.0f; songNoteOn = false; return; }
                }
                step = &songSteps[(size_t) (songStepIdx % (int) songSteps.size())];
                if (step->rest)
                {
                    songNoteOn = false;
                    songNoteVel = 0.0f;
                }
                else
                {
                    songNoteFreq = noteNameToFreq (step->note);
                    songNoteVel = step->vel;
                    songNoteOn = true;
                }
            }
        }

        void applyChorus (std::vector<float>& buf, int frames, const SynthParams& p)
        {
            const int maxDelaySamples = (int) (0.035 * vlab::kSampleRate); // 35ms ceiling
            if ((int) chorusBuf.size() != maxDelaySamples)
            {
                chorusBuf.assign ((size_t) maxDelaySamples, 0.0f);
                chorusWritePos = 0;
            }

            const float rate = juce::jmax (0.01f, p.chorusRate.load());
            const float depthSamples = juce::jlimit (0.0f, 1.0f, p.chorusDepth.load()) * maxDelaySamples * 0.6f;
            const float mix = p.chorusMix.load();
            const float baseDelaySamples = maxDelaySamples * 0.3f;

            for (int i = 0; i < frames; ++i)
            {
                chorusBuf[(size_t) chorusWritePos] = buf[(size_t) i];

                chorusPhase += 2.0 * juce::MathConstants<double>::pi * rate / vlab::kSampleRate;
                if (chorusPhase > 2.0 * juce::MathConstants<double>::pi) chorusPhase -= 2.0 * juce::MathConstants<double>::pi;
                const float lfo = std::sin ((float) chorusPhase);

                float readPos = (float) chorusWritePos - baseDelaySamples - lfo * depthSamples;
                while (readPos < 0.0f) readPos += (float) maxDelaySamples;

                const int r0 = (int) readPos % maxDelaySamples;
                const int r1 = (r0 + 1) % maxDelaySamples;
                const float frac = readPos - std::floor (readPos);
                const float wet = chorusBuf[(size_t) r0] * (1.0f - frac) + chorusBuf[(size_t) r1] * frac;

                buf[(size_t) i] = buf[(size_t) i] * (1.0f - mix) + wet * mix;
                chorusWritePos = (chorusWritePos + 1) % maxDelaySamples;
            }
        }

        void applyDelay (std::vector<float>& buf, int frames, const SynthParams& p)
        {
            const int maxDelaySamples = (int) (2.0 * vlab::kSampleRate); // 2s ceiling
            if ((int) delayBuf.size() != maxDelaySamples)
            {
                delayBuf.assign ((size_t) maxDelaySamples, 0.0f);
                delayWritePos = 0;
            }

            const int delaySamples = juce::jlimit (1, maxDelaySamples - 1,
                (int) (p.delayTimeMs.load() * 0.001f * (float) vlab::kSampleRate));
            const float feedback = juce::jlimit (0.0f, 0.95f, p.delayFeedback.load());
            const float mix = p.delayMix.load();

            for (int i = 0; i < frames; ++i)
            {
                const int readPos = (delayWritePos - delaySamples + maxDelaySamples) % maxDelaySamples;
                const float delayed = delayBuf[(size_t) readPos];

                delayBuf[(size_t) delayWritePos] = buf[(size_t) i] + delayed * feedback;
                buf[(size_t) i] = buf[(size_t) i] * (1.0f - mix) + delayed * mix;

                delayWritePos = (delayWritePos + 1) % maxDelaySamples;
            }
        }

        static void dsp_applyPan (vlab::AudioBuffer& buf, float pan, int n)
        {
            if (pan == 0.0f || buf.getNumChannels() < 2) return;
            const float theta = (pan + 1.0f) * (juce::MathConstants<float>::pi / 4.0f);
            buf.applyGain (0, 0, n, std::cos (theta));
            buf.applyGain (1, 0, n, std::sin (theta));
        }
    };
}
