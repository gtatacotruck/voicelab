#pragma once
#include "Common.h"
#include <array>
#include <cmath>

namespace vlab::dsp
{
    class HeadSpacePanner
    {
    public:
        HeadSpacePanner() { delayLine.setSize (2, kMaxDelaySamples); delayLine.clear(); }

        void process (vlab::AudioBuffer& buf, float azimuthDeg, float width, int numSamples)
        {
            if (width <= 0.0f && smoothedWidth <= 0.0001f)
                return;

            const float targetAz = juce::jlimit (-180.0f, 180.0f, azimuthDeg);
            const float targetWidth = juce::jlimit (0.0f, 1.0f, width);

            for (int i = 0; i < numSamples; ++i)
            {
                smoothedAz += (targetAz - smoothedAz) * smoothingCoeff;
                smoothedWidth += (targetWidth - smoothedWidth) * smoothingCoeff;

                const float az = smoothedAz * juce::MathConstants<float>::pi / 180.0f;
                const float s = std::sin (az) * smoothedWidth;

                const float leftGain  = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * s));
                const float rightGain = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * s));

                const float maxItdSamples = 0.0007f * (float) vlab::kSampleRate;
                const float delayLeft  = juce::jmax (0.0f,  s) * maxItdSamples;
                const float delayRight = juce::jmax (0.0f, -s) * maxItdSamples;

                const float mono = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                delayLine.setSample (0, writePos % kMaxDelaySamples, mono);
                delayLine.setSample (1, writePos % kMaxDelaySamples, mono);

                const float outL = readInterpolated (0, delayLeft);
                const float outR = readInterpolated (1, delayRight);

                const float farSideAmount = std::abs (s); // 0 = centered, 1 = fully to one side
                const float shadowAlpha = 1.0f - farSideAmount * 0.85f; // one-pole coefficient toward darker

                float finalL = outL * leftGain;
                float finalR = outR * rightGain;
                if (s > 0.0f) // source to the right -> left ear is "far", shadow it
                {
                    shadowStateL += (finalL - shadowStateL) * juce::jmax (0.05f, shadowAlpha);
                    finalL = shadowStateL;
                }
                else if (s < 0.0f)
                {
                    shadowStateR += (finalR - shadowStateR) * juce::jmax (0.05f, shadowAlpha);
                    finalR = shadowStateR;
                }
                else
                {
                    shadowStateL = finalL;
                    shadowStateR = finalR;
                }

                buf.setSample (0, i, finalL);
                buf.setSample (1, i, finalR);
                ++writePos;
            }
            writePos %= kMaxDelaySamples;
        }

    private:
        static constexpr int kMaxDelaySamples = 128; // > 0.7ms @ 44.1kHz (~31 samples), plenty of headroom
        vlab::AudioBuffer delayLine;
        int writePos = 0;
        float smoothedAz = 0.0f, smoothedWidth = 0.0f;
        float shadowStateL = 0.0f, shadowStateR = 0.0f;
        const float smoothingCoeff = 1.0f - std::exp (-1.0f / (0.005f * (float) vlab::kSampleRate));

        float readInterpolated (int ch, float delaySamples) const
        {
            const float readPos = (float) writePos - delaySamples;
            const int i0 = ((int) std::floor (readPos) % kMaxDelaySamples + kMaxDelaySamples) % kMaxDelaySamples;
            const int i1 = (i0 + 1) % kMaxDelaySamples;
            const float frac = readPos - std::floor (readPos);
            return delayLine.getSample (ch, i0) * (1.0f - frac) + delayLine.getSample (ch, i1) * frac;
        }
    };
}

