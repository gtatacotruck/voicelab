#pragma once
#include "Common.h"
#include <cmath>

namespace vlab::dsp
{
    class Compressor
    {
    public:
        void process (vlab::AudioBuffer& buf, float threshold, float ratio,
                       float attack, float release, int numSamples)
        {
            if (ratio <= 1.0f)
                return;

            const float ac = std::exp (-1.0f / (std::max (1e-4f, attack) * (float) vlab::kSampleRate));
            const float rc = std::exp (-1.0f / (std::max (1e-4f, release) * (float) vlab::kSampleRate));

            for (int i = 0; i < numSamples; ++i)
            {
                float level = 0.0f;
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    level = std::max (level, std::abs (buf.getSample (ch, i)));

                const float coeff = (level > env) ? ac : rc;
                env += (level - env) * (1.0f - coeff);

                const float over = std::max (0.0f, env - threshold);
                const float gr = (env > threshold)
                    ? 1.0f / (1.0f + (ratio - 1.0f) * over / std::max (threshold, 1e-6f))
                    : 1.0f;

                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    buf.setSample (ch, i, buf.getSample (ch, i) * gr);
            }
        }

    private:
        float env = 0.0f;
    };
}
