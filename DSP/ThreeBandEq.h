#pragma once
#include "Common.h"
#include <juce_dsp/juce_dsp.h>

namespace vlab::dsp
{
    class ThreeBandEq
    {
    public:
        ThreeBandEq()
        {
            for (auto& f : loFilters) f.prepare ({ vlab::kSampleRate, 512, 1 });
            for (auto& f : hiFilters) f.prepare ({ vlab::kSampleRate, 512, 1 });
            updateCoefficients();
        }

        void process (vlab::AudioBuffer& buf, float loDb, float midDb, float hiDb, int numSamples)
        {
            if (loDb == 0.0f && midDb == 0.0f && hiDb == 0.0f)
                return;

            const float loGain  = std::pow (10.0f, loDb / 20.0f);
            const float midGain = std::pow (10.0f, midDb / 20.0f);
            const float hiGain  = std::pow (10.0f, hiDb / 20.0f);

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = d[i];
                    const float lo = loFilters[(size_t) ch].processSample (x);
                    const float hi = hiFilters[(size_t) ch].processSample (x);
                    const float mid = x - lo - hi;
                    d[i] = lo * loGain + mid * midGain + hi * hiGain;
                }
            }
        }

    private:
        std::array<juce::dsp::IIR::Filter<float>, 2> loFilters, hiFilters;

        void updateCoefficients()
        {
            auto loCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (vlab::kSampleRate, 300.0f);
            auto hiCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (vlab::kSampleRate, 3000.0f);
            for (auto& f : loFilters) f.coefficients = loCoeffs;
            for (auto& f : hiFilters) f.coefficients = hiCoeffs;
        }
    };
}
