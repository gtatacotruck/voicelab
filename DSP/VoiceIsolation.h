#pragma once
// voice isolation: RNNoise plus a voice-activity gate on top. RNNoise wants 48k / 480 sample frames so the
// audio gets resampled up and back down around it. mono, no JUCE needed.
#include "../ThirdParty/rnnoise/rnnoise_unity.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vlab::dsp
{
    class VoiceIsolation
    {
    public:
        struct Settings
        {
            float strength = 1.0f;     // 0 = just the delayed input
            float reduceDb = 30.0f;    // how far to pull things down when there is no voice
            float sensitivity = 0.45f; // voice probability that counts as speech
        };

        // measured, keeps the dry copy lined up when strength < 1
        static constexpr int kLatencySamples = 896;

        VoiceIsolation() = default;
        VoiceIsolation (const VoiceIsolation&) = delete;
        VoiceIsolation& operator= (const VoiceIsolation&) = delete;
        ~VoiceIsolation() { destroy(); }

        void prepare (double sampleRateIn)
        {
            sampleRate = sampleRateIn;
            destroy();
            state = vlab_rnnoise::rnnoise_create (nullptr);
            reset();
        }

        void reset()
        {
            if (state != nullptr)
            {
                vlab_rnnoise::rnnoise_destroy (state);
                state = vlab_rnnoise::rnnoise_create (nullptr);
            }
            inRing.fill (0.0f);
            outRing.fill (0.0f);
            dryRing.fill (0.0f);
            wIn = 0;
            wOut = kPrefill;
            wDry = 0;
            upPos = 0.0;
            downPos = 0.0;
            frameFill = 0;
            gain = 1.0f;
            hangover = 0;
            voicedRun = 0;
            voiceProbability.store (0.0f);
        }

        void process (float* data, int n, const Settings& s)
        {
            if (state == nullptr)
                return;

            const float floorGain = std::pow (10.0f, -juce_clamp (s.reduceDb, 0.0f, 80.0f) / 20.0f);
            const float strength = juce_clamp (s.strength, 0.0f, 1.0f);
            const double upStep = sampleRate / 48000.0;
            const double downStep = 48000.0 / sampleRate;

            for (int i = 0; i < n; ++i)
            {
                inRing[(size_t) (wIn & kMask)] = data[i];
                dryRing[(size_t) (wDry & kMask)] = data[i];
                ++wIn;
                ++wDry;

                while (upPos + 2.0 < (double) wIn)
                {
                    frame[(size_t) frameFill++] = cubic (inRing, upPos) * 32768.0f;
                    upPos += upStep;
                    if (frameFill == kFrame)
                    {
                        frameFill = 0;
                        runFrame (s.sensitivity, floorGain);
                    }
                }

                float wet = 0.0f;
                if (downPos + 2.0 < (double) wOut)
                {
                    wet = cubic (outRing, downPos);
                    downPos += downStep;
                }

                const float dry = dryRing[(size_t) ((wDry - 1 - kLatencySamples) & kMask)];
                data[i] = strength >= 0.999f ? wet : dry * (1.0f - strength) + wet * strength;
            }
        }

        std::atomic<float> voiceProbability { 0.0f };

    private:
        static constexpr int kFrame = 480;
        static constexpr int kRing = 16384, kMask = kRing - 1;
        // so the down-sampler never runs dry between frames
        static constexpr int kPrefill = kFrame + 16;

        vlab_rnnoise::DenoiseState* state = nullptr;
        double sampleRate = 44100.0;

        std::array<float, kRing> inRing {}, outRing {}, dryRing {};
        std::array<float, kFrame> frame {}, processed {};
        int64_t wIn = 0, wOut = 0, wDry = 0;
        double upPos = 0.0, downPos = 0.0;
        int frameFill = 0;
        float gain = 1.0f;
        int hangover = 0, voicedRun = 0;

        static float juce_clamp (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        void destroy()
        {
            if (state != nullptr)
            {
                vlab_rnnoise::rnnoise_destroy (state);
                state = nullptr;
            }
        }

        static float cubic (const std::array<float, kRing>& ring, double pos)
        {
            const int64_t i = (int64_t) std::floor (pos);
            const float t = (float) (pos - (double) i);
            const float y0 = ring[(size_t) ((i - 1) & kMask)];
            const float y1 = ring[(size_t) (i & kMask)];
            const float y2 = ring[(size_t) ((i + 1) & kMask)];
            const float y3 = ring[(size_t) ((i + 2) & kMask)];
            return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
        }

        void runFrame (float sensitivity, float floorGain)
        {
            const float vad = vlab_rnnoise::rnnoise_process_frame (state, processed.data(), frame.data());
            voiceProbability.store (vad);

            // real speech holds the gate open ~80 ms so word endings survive, a single frame that scores as voice
            // (key click, cough) only gets 20 ms or every click would keep it open
            if (vad >= juce_clamp (sensitivity, 0.05f, 0.95f))
            {
                ++voicedRun;
                hangover = std::max (hangover, voicedRun >= 3 ? 8 : 2);
            }
            else
            {
                voicedRun = 0;
                if (hangover > 0)
                    --hangover;
            }

            const float target = hangover > 0 ? 1.0f : floorGain;
            const float attack = 1.0f - std::exp (-1.0f / (0.004f * 48000.0f));
            const float release = 1.0f - std::exp (-1.0f / (0.060f * 48000.0f));
            for (int i = 0; i < kFrame; ++i)
            {
                gain += (target - gain) * (target > gain ? attack : release);
                outRing[(size_t) (wOut & kMask)] = processed[(size_t) i] / 32768.0f * gain;
                ++wOut;
            }
        }
    };
}
