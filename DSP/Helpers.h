#pragma once
#include "Common.h"
#include <cmath>

namespace vlab::dsp
{
    // apply_gain(buf, gain_db)
    inline void applyGain (vlab::AudioBuffer& buf, float gainDb, int numSamples)
    {
        if (gainDb == 0.0f)
            return;
        const float g = std::pow (10.0f, gainDb / 20.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.applyGain (ch, 0, numSamples, g);
    }

    // apply_pan(buf, pan)  - pan in [-1, 1]
    inline void applyPan (vlab::AudioBuffer& buf, float pan, int numSamples)
    {
        if (pan == 0.0f || buf.getNumChannels() < 2)
            return;
        const float theta = (pan + 1.0f) * (juce::MathConstants<float>::pi / 4.0f);
        const float lGain = std::cos (theta);
        const float rGain = std::sin (theta);
        buf.applyGain (0, 0, numSamples, lGain);
        buf.applyGain (1, 0, numSamples, rGain);
    }

    // apply_distortion(buf, amount) - tanh-based drive, amount in [0, 1]
    inline void applyDistortion (vlab::AudioBuffer& buf, float amount, int numSamples)
    {
        if (amount <= 0.0f)
            return;
        const float t = 1.0f / (1.0f + amount * 10.0f);
        const float post = t * (1.0f + amount * 0.5f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] = std::tanh (d[i] / t) * post;
        }
    }

    // apply_distortion_soft(buf, drive) - softer alternative clip curve
    inline void applyDistortionSoft (vlab::AudioBuffer& buf, float drive, int numSamples)
    {
        if (drive <= 0.0f)
            return;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float x = d[i] * (1.0f + drive * 5.0f);
                d[i] = x / (1.0f + std::abs (x));
            }
        }
    }

    // apply_bitcrush(buf, bits)
    inline void applyBitcrush (vlab::AudioBuffer& buf, float bits, int numSamples)
    {
        if (bits <= 0.0f)
            return;
        const float steps = std::pow (2.0f, (float) std::max (1, (int) bits));
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] = std::round (d[i] * steps) / steps;
        }
    }

    class Tremolo
    {
    public:
        void process (vlab::AudioBuffer& buf, float rateHz, float depth, int numSamples)
        {
            if (depth <= 0.0f)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = 1.0f - depth * 0.5f *
                    (1.0f + std::sin (2.0f * juce::MathConstants<float>::pi * rateHz
                                        * (float) (phaseSamples + i) / (float) vlab::kSampleRate));
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    buf.setSample (ch, i, buf.getSample (ch, i) * lfo);
            }
            phaseSamples += numSamples;
        }

    private:
        juce::int64 phaseSamples = 0;
    };
}
