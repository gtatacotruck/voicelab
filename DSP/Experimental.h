#pragma once
#include "Common.h"
#include <cmath>
#include <algorithm>

namespace vlab::dsp
{
    class ExperimentalFx
    {
    public:
        void process (vlab::AudioBuffer& x, int numSamples,
                      bool deEsserOn, float deEsserAmount,
                      bool autoLevelOn, float autoLevelAmount,
                      bool underwaterOn, float underwaterAmount,
                      bool presenceOn, float presenceAmount,
                      bool radioSweepOn, float radioSweepRate, float radioSweepAmount,
                      bool pumpOn, float pumpRate, float pumpDepth)
        {
            if (deEsserOn)
                applyDeEsser (x, numSamples, deEsserAmount);

            if (autoLevelOn)
                applyAutoLevel (x, numSamples, autoLevelAmount);

            if (underwaterOn)
                applyUnderwater (x, numSamples, underwaterAmount);

            if (presenceOn)
                applyPresenceBoost (x, numSamples, presenceAmount);

            if (radioSweepOn)
                applyRadioSweep (x, numSamples, radioSweepRate, radioSweepAmount);

            if (pumpOn)
                applyPump (x, numSamples, pumpRate, pumpDepth);
        }

    private:
        float deEsserLp[2] = { 0.0f, 0.0f };

        void applyDeEsser (vlab::AudioBuffer& x, int n, float amount)
        {
            const float threshold = 0.15f * (1.0f - juce::jlimit (0.0f, 1.0f, amount) * 0.7f);
            for (int ch = 0; ch < 2; ++ch)
            {
                float lp = deEsserLp[ch];
                for (int i = 0; i < n; ++i)
                {
                    const float in = x.getSample (ch, i);
                    lp += (in - lp) * 0.15f;
                    const float high = in - lp;
                    const float highLevel = std::abs (high);
                    const float gain = (highLevel > threshold) ? juce::jmax (0.15f, threshold / highLevel) : 1.0f;
                    x.setSample (ch, i, lp + high * gain);
                }
                deEsserLp[ch] = lp;
            }
        }

        float autoLevelEnv = 0.0f, autoLevelGain = 1.0f;

        void applyAutoLevel (vlab::AudioBuffer& x, int n, float amount)
        {
            constexpr float targetLevel = 0.2f;
            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (x.getSample (0, i)), std::abs (x.getSample (1, i)));
                autoLevelEnv += (peak - autoLevelEnv) * 0.001f;
                const float wanted = autoLevelEnv > 1e-4f ? targetLevel / autoLevelEnv : 1.0f;
                autoLevelGain += (juce::jlimit (0.3f, 3.0f, wanted) - autoLevelGain) * 0.0005f;
                const float g = 1.0f + (autoLevelGain - 1.0f) * juce::jlimit (0.0f, 1.0f, amount);
                x.setSample (0, i, x.getSample (0, i) * g);
                x.setSample (1, i, x.getSample (1, i) * g);
            }
        }

        float underwaterLp[2] = { 0.0f, 0.0f };
        float underwaterPhase = 0.0f;

        void applyUnderwater (vlab::AudioBuffer& x, int n, float amount)
        {
            const float amt = juce::jlimit (0.0f, 1.0f, amount);
            for (int i = 0; i < n; ++i)
            {
                underwaterPhase += 2.0f * juce::MathConstants<float>::pi * 0.25f / (float) vlab::kSampleRate;
                if (underwaterPhase > 2.0f * juce::MathConstants<float>::pi)
                    underwaterPhase -= 2.0f * juce::MathConstants<float>::pi;
                const float coeff = 0.05f + 0.10f * (0.5f + 0.5f * std::sin (underwaterPhase));

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = x.getSample (ch, i);
                    underwaterLp[ch] += (in - underwaterLp[ch]) * coeff;
                    x.setSample (ch, i, in * (1.0f - amt) + underwaterLp[ch] * amt);
                }
            }
        }

        float presenceLp[2] = { 0.0f, 0.0f };
        float presenceEnv = 0.0f;

        void applyPresenceBoost (vlab::AudioBuffer& x, int n, float amount)
        {
            const float amt = juce::jlimit (0.0f, 1.0f, amount);
            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (x.getSample (0, i)), std::abs (x.getSample (1, i)));
                presenceEnv += (peak - presenceEnv) * 0.002f;
                const float adaptiveBoost = 1.0f + (1.0f - juce::jlimit (0.0f, 1.0f, presenceEnv * 3.0f)) * 0.6f;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = x.getSample (ch, i);
                    presenceLp[ch] += (in - presenceLp[ch]) * 0.25f;
                    const float high = in - presenceLp[ch];
                    x.setSample (ch, i, in + high * amt * adaptiveBoost * 0.8f);
                }
            }
        }

        float radioLp1[2] = { 0.0f, 0.0f }, radioLp2[2] = { 0.0f, 0.0f };
        float radioPhase = 0.0f;

        void applyRadioSweep (vlab::AudioBuffer& x, int n, float rateHz, float amount)
        {
            const float amt = juce::jlimit (0.0f, 1.0f, amount);
            for (int i = 0; i < n; ++i)
            {
                radioPhase += 2.0f * juce::MathConstants<float>::pi * juce::jmax (0.01f, rateHz) / (float) vlab::kSampleRate;
                if (radioPhase > 2.0f * juce::MathConstants<float>::pi)
                    radioPhase -= 2.0f * juce::MathConstants<float>::pi;
                const float centre = 0.02f + 0.12f * (0.5f + 0.5f * std::sin (radioPhase));

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = x.getSample (ch, i);
                    radioLp1[ch] += (in - radioLp1[ch]) * centre;
                    radioLp2[ch] += (radioLp1[ch] - radioLp2[ch]) * (centre * 0.4f);
                    const float band = radioLp1[ch] - radioLp2[ch];
                    const float gritty = std::tanh (band * 3.0f) * 0.7f;
                    x.setSample (ch, i, in * (1.0f - amt) + gritty * amt);
                }
            }
        }

        float pumpPhase = 0.0f;

        void applyPump (vlab::AudioBuffer& x, int n, float rateHz, float depth)
        {
            const float d = juce::jlimit (0.0f, 1.0f, depth);
            for (int i = 0; i < n; ++i)
            {
                pumpPhase += juce::jmax (0.05f, rateHz) / (float) vlab::kSampleRate;
                if (pumpPhase > 1.0f) pumpPhase -= 1.0f;
                const float env = 1.0f - d * std::exp (-pumpPhase * 8.0f);
                x.setSample (0, i, x.getSample (0, i) * env);
                x.setSample (1, i, x.getSample (1, i) * env);
            }
        }
    };
}
