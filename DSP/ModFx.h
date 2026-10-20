#pragma once
#include "Common.h"
#include <cmath>

namespace vlab::dsp
{
    class Chorus
    {
    public:
        Chorus() { buf.setSize (2, maxDelay); buf.clear(); }

        void process (vlab::AudioBuffer& b, float rate, float depth, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = (std::sin (phase + (float) i * 2.0f * juce::MathConstants<float>::pi
                                              * rate / (float) vlab::kSampleRate) + 1.0f) * 0.5f;
                int delaySamples = juce::jlimit (1, maxDelay - 1,
                    (int) ((0.01f + 0.02f * depth) * (float) vlab::kSampleRate * lfo));

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = b.getSample (ch, i);
                    buf.setSample (ch, wptr % maxDelay, in);
                    const int ri = ((wptr - delaySamples) % maxDelay + maxDelay) % maxDelay;
                    const float out = in * (1.0f - mix) + buf.getSample (ch, ri) * mix;
                    b.setSample (ch, i, out);
                }
                ++wptr;
            }
            wptr %= maxDelay;
            phase = std::fmod (phase + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                * rate / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        static constexpr int maxDelay = (int) (vlab::kSampleRate * 0.04);
        vlab::AudioBuffer buf;
        int wptr = 0;
        float phase = 0.0f;
    };

    class Delay
    {
    public:
        Delay() { buf.setSize (2, maxDelay); buf.clear(); }

        void process (vlab::AudioBuffer& b, float t1, float t2, float fb, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            const int d1 = std::max (1, (int) (t1 * vlab::kSampleRate));
            const int d2 = std::max (1, (int) (t2 * vlab::kSampleRate));

            for (int i = 0; i < numSamples; ++i)
            {
                const int r1 = ((wptr - d1) % maxDelay + maxDelay) % maxDelay;
                const int r2 = ((wptr - d2) % maxDelay + maxDelay) % maxDelay;
                const float ppL = buf.getSample (0, r1);
                const float ppR = buf.getSample (1, r2);

                const float inL = b.getSample (0, i);
                const float inR = b.getSample (1, i);
                buf.setSample (0, wptr % maxDelay, inL + ppL * fb);
                buf.setSample (1, wptr % maxDelay, inR + ppR * fb);

                b.setSample (0, i, inL * (1.0f - mix) + ppL * mix);
                b.setSample (1, i, inR * (1.0f - mix) + ppR * mix);
                ++wptr;
            }
            wptr %= maxDelay;
        }

    private:
        static constexpr int maxDelay = (int) (vlab::kSampleRate * 2.0);
        vlab::AudioBuffer buf;
        int wptr = 0;
    };

    class Phaser
    {
    public:
        void process (vlab::AudioBuffer& b, float rate, float depth, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = 200.0f + (std::sin (phase + (float) i * 2.0f * juce::MathConstants<float>::pi
                                             * rate / (float) vlab::kSampleRate) + 1.0f) * 0.5f * depth * 4000.0f;
                const float tanArg = std::tan (juce::MathConstants<float>::pi * lfo / (float) vlab::kSampleRate);
                const float a = (tanArg - 1.0f) / (tanArg + 1.0f);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = b.getSample (ch, i);
                    const float ap = a * in + z[ch];
                    z[ch] = in - a * ap;
                    b.setSample (ch, i, in * (1.0f - mix) + ap * mix);
                }
            }
            phase = std::fmod (phase + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                * rate / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        float z[2] = { 0.0f, 0.0f };
        float phase = 0.0f;
    };
}
