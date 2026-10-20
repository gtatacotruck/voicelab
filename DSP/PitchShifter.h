#pragma once
#include "Common.h"
#include <cmath>
#include <vector>

namespace vlab::dsp
{
    // Two-tap sweeping delay line. Each tap's delay slides by (1 - rate) samples per sample and
    // wraps, the two taps sit half a sweep apart and are cross-faded with a Hann window so the
    // wrap point is always silent. Delay never goes negative, so any ratio up to +/-24 st is safe.
    // When a tap restarts its sweep it is nudged to the offset whose recent waveform best matches
    // the other tap, so the cross-fade adds in phase instead of cancelling (no level warble).
    class PitchShifter
    {
    public:
        void process (const vlab::AudioBuffer& in, vlab::AudioBuffer& out, float semitones, int numSamples)
        {
            if (ring.getNumSamples() != ringLen)
            {
                ring.setSize (2, ringLen);
                ring.clear();
                mono.assign ((size_t) ringLen, 0.0f);
                writePos = 0;
                phase = 0.0;
                mix = 0.0f;
                offset[0] = offset[1] = 0;
                lastP[0] = 0.0;
                lastP[1] = 0.5;
            }

            const float semis = juce::jlimit (-24.0f, 24.0f, semitones);
            const bool wantWet = std::abs (semis) >= 0.02f;
            const double rate = std::pow (2.0, (double) semis / 12.0);
            const double phaseInc = (1.0 - rate) / (double) windowLen;
            constexpr float mixStep = 1.0f / 1024.0f;

            const int inCh = in.getNumChannels();
            const float* inL = in.getReadPointer (0);
            const float* inR = in.getReadPointer (inCh > 1 ? 1 : 0);
            float* outL = out.getWritePointer (0);
            float* outR = out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0);
            float* ringL = ring.getWritePointer (0);
            float* ringR = ring.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const float dryL = inL[i], dryR = inR[i];
                ringL[writePos] = dryL;
                ringR[writePos] = dryR;

                mono[(size_t) writePos] = 0.5f * (dryL + dryR);

                mix = wantWet ? juce::jmin (1.0f, mix + mixStep) : juce::jmax (0.0f, mix - mixStep);

                double tapP[2] = { phase, phase + 0.5 };
                if (tapP[1] >= 1.0) tapP[1] -= 1.0;
                for (int tap = 0; tap < 2; ++tap)
                {
                    if (std::abs (tapP[tap] - lastP[tap]) > 0.5 && mix > 0.0f)
                        alignTap (tap, tapP[1 - tap], tapP[tap]);
                    lastP[tap] = tapP[tap];
                }

                float l = dryL, r = dryR;
                if (mix > 0.0f)
                {
                    float wetL = 0.0f, wetR = 0.0f;
                    for (int tap = 0; tap < 2; ++tap)
                    {
                        const double p = tapP[tap];
                        const float w = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * p);
                        const double delay = delayFor (p, offset[tap]);
                        float a, b;
                        readHermite (delay, a, b);
                        wetL += a * w;
                        wetR += b * w;
                    }
                    l = dryL * (1.0f - mix) + wetL * mix;
                    r = dryR * (1.0f - mix) + wetR * mix;
                }
                outL[i] = l;
                outR[i] = r;

                phase += phaseInc;
                phase -= std::floor (phase);
                if (++writePos >= ringLen) writePos = 0;
            }
        }

    private:
        static constexpr int ringLen = (int) 44100;
        static constexpr int windowLen = 1536; // ~35ms sweep

        static constexpr int searchRange = 320;  // +/- samples a restarting tap may slide to find a match
        static constexpr int corrLen = 512;      // history compared when matching

        vlab::AudioBuffer ring;
        std::vector<float> mono;
        int offset[2] = { 0, 0 };
        double lastP[2] = { 0.0, 0.5 };
        int writePos = 0;
        double phase = 0.0;
        float mix = 0.0f;

        static double delayFor (double p, int off)
        {
            return (double) (searchRange + 2 + off) + p * (double) windowLen;
        }

        int wrapIdx (int i) const
        {
            while (i < 0) i += ringLen;
            while (i >= ringLen) i -= ringLen;
            return i;
        }

        float matchScore (int posCand, int posRef) const
        {
            float dot = 0.0f, energy = 1.0e-9f;
            for (int k = 0; k < corrLen; k += 2)
            {
                const float a = mono[(size_t) wrapIdx (posCand - k)];
                const float b = mono[(size_t) wrapIdx (posRef - k)];
                dot += a * b;
                energy += a * a;
            }
            return dot / std::sqrt (energy);
        }

        void alignTap (int tap, double otherP, double newP)
        {
            const int posRef = wrapIdx (writePos - (int) std::lround (delayFor (otherP, offset[1 - tap])));
            const double base = delayFor (newP, 0);
            auto scoreAt = [&] (int o) { return matchScore (wrapIdx (writePos - (int) std::lround (base + o)), posRef); };

            int bestO = 0;
            float best = -1.0e30f;
            for (int o = -searchRange; o <= searchRange; o += 2)
            {
                const float sc = scoreAt (o);
                if (sc > best) { best = sc; bestO = o; }
            }
            for (int o : { bestO - 1, bestO + 1 })
            {
                if (o < -searchRange || o > searchRange) continue;
                const float sc = scoreAt (o);
                if (sc > best) { best = sc; bestO = o; }
            }
            offset[tap] = bestO;
        }

        void readHermite (double delay, float& outL, float& outR) const
        {
            double pos = (double) writePos - delay;
            const double fl = std::floor (pos);
            const float f = (float) (pos - fl);
            int i1 = (int) fl % ringLen;
            if (i1 < 0) i1 += ringLen;
            const int i0 = (i1 + ringLen - 1) % ringLen;
            const int i2 = (i1 + 1) % ringLen;
            const int i3 = (i1 + 2) % ringLen;

            auto interp = [&] (const float* d)
            {
                const float y0 = d[i0], y1 = d[i1], y2 = d[i2], y3 = d[i3];
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * f + c2) * f + c1) * f + y1;
            };
            outL = interp (ring.getReadPointer (0));
            outR = interp (ring.getReadPointer (1));
        }
    };
}
