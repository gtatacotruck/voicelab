#pragma once
#include "Common.h"
#include <cmath>

namespace vlab::dsp
{
    // Short modulated delay with feedback, classic jet-swoosh flanger.
    class Flanger
    {
    public:
        Flanger() { buf.setSize (2, maxDelay); buf.clear(); }

        void process (vlab::AudioBuffer& b, float rate, float depth, float feedback, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = (std::sin (phase + (float) i * 2.0f * juce::MathConstants<float>::pi
                                              * rate / (float) vlab::kSampleRate) + 1.0f) * 0.5f;
                const int delaySamples = juce::jlimit (1, maxDelay - 1,
                    (int) ((0.001f + 0.006f * depth) * (float) vlab::kSampleRate * lfo));

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = b.getSample (ch, i);
                    const int ri = ((wptr - delaySamples) % maxDelay + maxDelay) % maxDelay;
                    const float delayed = buf.getSample (ch, ri);
                    buf.setSample (ch, wptr % maxDelay, in + delayed * feedback);
                    b.setSample (ch, i, in * (1.0f - mix) + delayed * mix);
                }
                ++wptr;
            }
            wptr %= maxDelay;
            phase = std::fmod (phase + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                * rate / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        static constexpr int maxDelay = (int) (vlab::kSampleRate * 0.01);
        vlab::AudioBuffer buf;
        int wptr = 0;
        float phase = 0.0f;
    };

    class AutoWah
    {
    public:
        void process (vlab::AudioBuffer& b, float sensitivity, float rate, float depth, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                float level = 0.0f;
                for (int ch = 0; ch < 2; ++ch) level = std::max (level, std::abs (b.getSample (ch, i)));
                env += (level - env) * 0.05f;

                const float lfo = (std::sin (lfoPhase) + 1.0f) * 0.5f;
                lfoPhase += 2.0f * juce::MathConstants<float>::pi * rate / (float) vlab::kSampleRate;
                if (lfoPhase > 2.0f * juce::MathConstants<float>::pi) lfoPhase -= 2.0f * juce::MathConstants<float>::pi;

                const float centreHz = 400.0f + (env * sensitivity * 2500.0f) + lfo * depth * 1500.0f;
                const float w0 = 2.0f * juce::MathConstants<float>::pi * juce::jlimit (100.0f, 6000.0f, centreHz)
                                  / (float) vlab::kSampleRate;
                const float alpha = std::sin (w0) * 0.7f; // fixed Q
                const float cosw0 = std::cos (w0);
                const float b0 = alpha, a0 = 1.0f + alpha, a1 = -2.0f * cosw0, a2 = 1.0f - alpha;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x0 = b.getSample (ch, i);
                    const float y0 = (b0 * x0 - a1 * y1[ch] - a2 * y2[ch]) / a0;
                    y2[ch] = y1[ch]; y1[ch] = y0;
                    b.setSample (ch, i, x0 * (1.0f - mix) + y0 * mix);
                }
            }
        }

    private:
        float env = 0.0f, lfoPhase = 0.0f;
        float y1[2] = { 0, 0 }, y2[2] = { 0, 0 };
    };

    class Vibrato
    {
    public:
        Vibrato() { buf.setSize (2, maxDelay); buf.clear(); }

        void process (vlab::AudioBuffer& b, float rate, float depth, int numSamples)
        {
            if (depth <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = std::sin (phase + (float) i * 2.0f * juce::MathConstants<float>::pi
                                             * rate / (float) vlab::kSampleRate);
                const float delaySamples = (5.0f + lfo * depth * 4.0f) * (float) vlab::kSampleRate / 1000.0f;
                const float readPos = (float) wptr - delaySamples;
                const int i0 = ((int) std::floor (readPos) % maxDelay + maxDelay) % maxDelay;
                const int i1 = (i0 + 1) % maxDelay;
                const float frac = readPos - std::floor (readPos);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = b.getSample (ch, i);
                    buf.setSample (ch, wptr % maxDelay, in);
                    const float out = buf.getSample (ch, i0) * (1.0f - frac) + buf.getSample (ch, i1) * frac;
                    b.setSample (ch, i, out);
                }
                ++wptr;
            }
            wptr %= maxDelay;
            phase = std::fmod (phase + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                * rate / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        static constexpr int maxDelay = (int) (vlab::kSampleRate * 0.05);
        vlab::AudioBuffer buf;
        int wptr = 0;
        float phase = 0.0f;
    };

    // Slowly sweeps the stereo pan position via an LFO.
    class AutoPan
    {
    public:
        void process (vlab::AudioBuffer& b, float rate, float depth, int numSamples)
        {
            if (depth <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = std::sin (phase + (float) i * 2.0f * juce::MathConstants<float>::pi
                                             * rate / (float) vlab::kSampleRate) * depth;
                const float theta = (lfo + 1.0f) * (juce::MathConstants<float>::pi / 4.0f);
                b.setSample (0, i, b.getSample (0, i) * std::cos (theta));
                b.setSample (1, i, b.getSample (1, i) * std::sin (theta));
            }
            phase = std::fmod (phase + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                * rate / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        float phase = 0.0f;
    };

    class Telephone
    {
    public:
        void process (vlab::AudioBuffer& b, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            const float hpCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 300.0f / (float) vlab::kSampleRate);
            const float lpCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 3400.0f / (float) vlab::kSampleRate);

            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = b.getSample (ch, i);

                    // One-pole highpass: y = x - lp(x)
                    hpState[ch] = hpCoeff * hpState[ch] + (1.0f - hpCoeff) * x;
                    const float hp = x - hpState[ch];

                    // One-pole lowpass on the highpassed signal
                    lpState[ch] = lpCoeff * lpState[ch] + (1.0f - lpCoeff) * hp;

                    const float shaped = std::tanh (lpState[ch] * 2.5f) * 1.6f;

                    b.setSample (ch, i, x * (1.0f - mix) + shaped * mix);
                }
            }
        }

    private:
        float hpState[2] = { 0, 0 };
        float lpState[2] = { 0, 0 };
    };

    class Doubler
    {
    public:
        Doubler() { buf.setSize (2, maxDelay); buf.clear(); }

        void process (vlab::AudioBuffer& b, float detuneCents, float delayMs, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            const float lfoRateHz = 0.15f;

            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float sign = (ch == 0) ? 1.0f : -1.0f;
                    const float lfo = std::sin (phase[ch] + (float) i * 2.0f * juce::MathConstants<float>::pi
                                                 * lfoRateHz / (float) vlab::kSampleRate);
                    const float centsNow = sign * detuneCents * (0.7f + 0.3f * lfo);
                    const float rate = std::pow (2.0f, centsNow / 1200.0f);

                    const float baseDelaySamples = (delayMs + (sign * 3.0f)) * (float) vlab::kSampleRate / 1000.0f;
                    readPos[ch] = std::fmod (readPos[ch] + (rate - 1.0f), (float) maxDelay); // wrap so this can't grow unbounded over a long-running session
                    const float target = (float) wptr - baseDelaySamples - readPos[ch];
                    const int i0 = ((int) std::floor (target) % maxDelay + maxDelay) % maxDelay;
                    const int i1 = (i0 + 1) % maxDelay;
                    const float frac = target - std::floor (target);

                    const float in = b.getSample (ch, i);
                    buf.setSample (ch, wptr % maxDelay, in);
                    const float doubled = buf.getSample (ch, i0) * (1.0f - frac) + buf.getSample (ch, i1) * frac;
                    b.setSample (ch, i, in * (1.0f - mix * 0.5f) + doubled * mix * 0.5f);
                }
                ++wptr;
            }
            wptr %= maxDelay;
            for (int ch = 0; ch < 2; ++ch)
                phase[ch] = std::fmod (phase[ch] + (float) numSamples * 2.0f * juce::MathConstants<float>::pi
                                        * lfoRateHz / (float) vlab::kSampleRate, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        static constexpr int maxDelay = (int) (vlab::kSampleRate * 0.08);
        vlab::AudioBuffer buf;
        int wptr = 0;
        float phase[2] = { 0, juce::MathConstants<float>::pi }; // opposite phase per side
        float readPos[2] = { 0, 0 };
    };
}
