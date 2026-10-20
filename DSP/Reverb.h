#pragma once
#include "Common.h"
#include <array>
#include <vector>

namespace vlab::dsp
{
    class Reverb
    {
    public:
        Reverb() { prepare(); }

        void process (vlab::AudioBuffer& buf, float mix, float decay, int numSamples)
        {
            if (mix <= 0.0f)
                return;

            wet.setSize (2, numSamples, false, true, true);
            wet.clear();

            // --- comb filters (parallel, averaged) ---
            for (int c = 0; c < kNumCombs; ++c)
            {
                auto& cbuf = combBuf[c];
                const int d = combDelay[c];
                int ptr = combPtr[c];

                for (int i = 0; i < numSamples; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float in = buf.getSample (ch, i);
                        const float fb = cbuf.getSample (ch, ptr) * decay;
                        cbuf.setSample (ch, ptr, in + fb);
                        wet.addSample (ch, i, fb / (float) kNumCombs);
                    }
                    ptr = (ptr + 1) % d;
                }
                combPtr[c] = ptr;
            }

            // --- allpass filters (series) ---
            for (int a = 0; a < kNumAllpass; ++a)
            {
                auto& abuf = apBuf[a];
                const int d = apDelay[a];
                int ptr = apPtr[a];

                for (int i = 0; i < numSamples; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float w = wet.getSample (ch, i);
                        const float delayed = abuf.getSample (ch, ptr);
                        const float out = -w + delayed;
                        abuf.setSample (ch, ptr, w + delayed * 0.5f);
                        wet.setSample (ch, i, out);
                    }
                    ptr = (ptr + 1) % d;
                }
                apPtr[a] = ptr;
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    buf.setSample (ch, i, buf.getSample (ch, i) * (1.0f - mix) + wet.getSample (ch, i) * mix);
        }

    private:
        static constexpr int kNumCombs = 4;
        static constexpr int kNumAllpass = 2;
        static constexpr double combTimes[kNumCombs] = { 0.0297, 0.0371, 0.0411, 0.0437 };
        static constexpr double apTimes[kNumAllpass]  = { 0.005, 0.0017 };

        std::array<int, kNumCombs> combDelay {};
        std::array<int, kNumAllpass> apDelay {};
        std::array<vlab::AudioBuffer, kNumCombs> combBuf;
        std::array<vlab::AudioBuffer, kNumAllpass> apBuf;
        std::array<int, kNumCombs> combPtr {};
        std::array<int, kNumAllpass> apPtr {};
        vlab::AudioBuffer wet;

        void prepare()
        {
            for (int c = 0; c < kNumCombs; ++c)
            {
                combDelay[c] = std::max (1, (int) (vlab::kSampleRate * combTimes[c]));
                combBuf[c].setSize (2, combDelay[c]);
                combBuf[c].clear();
                combPtr[c] = 0;
            }
            for (int a = 0; a < kNumAllpass; ++a)
            {
                apDelay[a] = std::max (1, (int) (vlab::kSampleRate * apTimes[a]));
                apBuf[a].setSize (2, apDelay[a]);
                apBuf[a].clear();
                apPtr[a] = 0;
            }
        }
    };
}
