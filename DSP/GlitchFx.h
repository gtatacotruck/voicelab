#pragma once
#include "Common.h"
#include <cmath>
#include <vector>
#include <utility>

namespace vlab::dsp
{
    enum class StutterType { Loop, ReverseBounce, MultiSlice, HalfTime, SilenceGaps, Zap };

    class Stutter
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, StutterType type, float ratePerSecond,
                       float holdMs, float randomness, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                state = Idle;
                return;
            }

            const float rand01 = juce::jlimit (0.0f, 1.0f, randomness);

            for (int i = 0; i < numSamples; ++i)
            {
                if (state == Idle)
                {
                    if (idleSamplesLeft <= 0)
                        startTrigger (holdMs, rand01);
                    else
                    {
                        --idleSamplesLeft;
                        continue;
                    }
                }

                if (state == Capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= holdSamples)
                        startRepeat (type, rand01);
                    continue;
                }

                float wetL = 0.0f, wetR = 0.0f;
                const bool silentRepeat = (type == StutterType::SilenceGaps && repeatIsSilent);

                if (! silentRepeat)
                {
                    if (type == StutterType::HalfTime || type == StutterType::Zap)
                    {
                        const int idx = juce::jlimit (0, holdSamples - 1, (int) playPos);
                        const int idxNext = juce::jmin (idx + 1, holdSamples - 1);
                        const float frac = playPos - (float) idx;
                        wetL = captureBuf.getSample (0, idx) * (1.0f - frac) + captureBuf.getSample (0, idxNext) * frac;
                        wetR = captureBuf.getSample (1, idx) * (1.0f - frac) + captureBuf.getSample (1, idxNext) * frac;
                    }
                    else
                    {
                        const int idx = juce::jlimit (0, holdSamples - 1, (int) playPos);
                        wetL = captureBuf.getSample (0, idx);
                        wetR = captureBuf.getSample (1, idx);
                    }
                }

                buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                advance (type);

                if (repeatFinished)
                {
                    ++repeatIndex;
                    if (repeatIndex >= repeatsThisTrigger)
                    {
                        state = Idle;
                        const float avgGapSecs = ratePerSecond > 0.0f ? 1.0f / ratePerSecond : 1.0f;
                        idleSamplesLeft = (int) (juce::jmax (0.03f, avgGapSecs * (0.3f + rng.nextFloat() * (1.0f + rand01 * 2.0f))) * vlab::kSampleRate);
                    }
                    else
                    {
                        startRepeat (type, rand01);
                    }
                }
            }
        }

    private:
        enum State { Idle, Capturing, Playing };
        State state = Idle;

        vlab::AudioBuffer captureBuf;
        int holdSamples = 0, capturePos = 0;
        int idleSamplesLeft = 0;
        int repeatIndex = 0, repeatsThisTrigger = 1;
        float playPos = 0.0f;
        int playDir = 1;
        bool repeatIsSilent = false, repeatFinished = false;
        int sliceOffset = 0, sliceLen = 0;
        juce::Random rng;

        void startTrigger (float holdMs, float rand01)
        {
            const float jitter = 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * rand01 * 0.6f;
            holdSamples = juce::jmax (64, (int) (holdMs * 0.001f * vlab::kSampleRate * jitter));
            if (captureBuf.getNumSamples() < holdSamples)
                captureBuf.setSize (2, holdSamples, false, false, true);
            capturePos = 0;
            state = Capturing;
            repeatIndex = 0;
            repeatsThisTrigger = juce::jmax (1, 2 + rng.nextInt (1 + (int) (rand01 * 8)));
        }

        void startRepeat (StutterType type, float rand01)
        {
            state = Playing;
            repeatFinished = false;
            playDir = (type == StutterType::ReverseBounce && (repeatIndex % 2 == 1)) ? -1 : 1;
            playPos = (playDir > 0) ? 0.0f : (float) (holdSamples - 1);

            if (type == StutterType::MultiSlice)
            {
                sliceLen = juce::jmax (32, holdSamples / 4);
                sliceOffset = rng.nextInt (juce::jmax (1, holdSamples - sliceLen));
                playPos = (float) sliceOffset;
            }

            repeatIsSilent = (type == StutterType::SilenceGaps) && (rng.nextFloat() < 0.3f * (0.4f + rand01));
        }

        void advance (StutterType type)
        {
            float speed = 1.0f;
            if (type == StutterType::HalfTime)
                speed = 0.5f;
            else if (type == StutterType::Zap)
            {
                const float progress = juce::jlimit (0.0f, 1.0f, playPos / (float) juce::jmax (1, holdSamples));
                speed = 0.2f + progress * 3.8f;
            }
            playPos += playDir * speed;

            if (type == StutterType::MultiSlice)
                repeatFinished = (playPos >= (float) (sliceOffset + sliceLen) || playPos < (float) sliceOffset);
            else if (playDir > 0)
                repeatFinished = (playPos >= (float) holdSamples);
            else
                repeatFinished = (playPos < 0.0f);
        }
    };

    class RingModulator
    {
    public:
        void process (vlab::AudioBuffer& buf, float freqHz, float mix, int numSamples)
        {
            if (mix <= 0.0f)
                return;
            const float inc = 2.0f * juce::MathConstants<float>::pi * freqHz / (float) vlab::kSampleRate;
            for (int i = 0; i < numSamples; ++i)
            {
                const float carrier = std::sin (phase + i * inc);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float dry = buf.getSample (ch, i);
                    buf.setSample (ch, i, dry * (1.0f - mix) + (dry * carrier) * mix);
                }
            }
            phase = std::fmod (phase + numSamples * inc, 2.0f * juce::MathConstants<float>::pi);
        }

    private:
        float phase = 0.0f;
    };

    class EscalatingRepeat
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, float growthPerRepeat, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                wasEnabled = false;
                return;
            }

            const int holdSamples = juce::jmax (64, (int) (holdMs * 0.001f * vlab::kSampleRate));
            if (! wasEnabled || captureBuf.getNumSamples() != holdSamples)
            {
                captureBuf.setSize (2, holdSamples);
                captureBuf.clear();
                capturePos = 0;
                capturing = true;
            }
            wasEnabled = true;

            const float growth = juce::jlimit (1.0f, 2.0f, growthPerRepeat);
            // High ceiling (~+15.6dB), not a subtle safety clamp.
            constexpr float kMaxGain = 6.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= holdSamples) { capturing = false; loopPos = 0; loopCount = 0; }
                }
                else
                {
                    const float gain = juce::jmin (kMaxGain, std::pow (growth, (float) loopCount));
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + captureBuf.getSample (0, loopPos) * gain * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + captureBuf.getSample (1, loopPos) * gain * mix);
                    if (++loopPos >= holdSamples) { loopPos = 0; ++loopCount; }
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int capturePos = 0, loopPos = 0, loopCount = 0;
    };

    class AcceleratingRepeat
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, float speedGrowthPerLoop, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                wasEnabled = false;
                return;
            }

            const int holdSamples = juce::jmax (64, (int) (holdMs * 0.001f * vlab::kSampleRate));
            if (! wasEnabled || captureBuf.getNumSamples() != holdSamples)
            {
                captureBuf.setSize (2, holdSamples);
                captureBuf.clear();
                capturePos = 0;
                capturing = true;
                readPos = 0.0f;
                speed = 1.0f;
            }
            wasEnabled = true;

            const float growth = juce::jlimit (1.0f, 2.0f, speedGrowthPerLoop);
            // Ceiling past which it's inaudibly high anyway.
            constexpr float kMaxSpeed = 48.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= holdSamples) { capturing = false; readPos = 0.0f; }
                }
                else
                {
                    const int idx = (int) readPos;
                    const int idxNext = (idx + 1) % holdSamples;
                    const float frac = readPos - (float) idx;
                    const float wetL = captureBuf.getSample (0, idx) * (1.0f - frac) + captureBuf.getSample (0, idxNext) * frac;
                    const float wetR = captureBuf.getSample (1, idx) * (1.0f - frac) + captureBuf.getSample (1, idxNext) * frac;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                    readPos += speed;
                    if (readPos >= (float) holdSamples)
                    {
                        readPos = std::fmod (readPos, (float) holdSamples);
                        speed = juce::jmin (kMaxSpeed, speed * growth);
                    }
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int capturePos = 0;
        float readPos = 0.0f, speed = 1.0f;
    };

    class DecelerateRepeat
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, float speedShrinkPerLoop, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f) { wasEnabled = false; return; }

            const int holdSamples = juce::jmax (64, (int) (holdMs * 0.001f * vlab::kSampleRate));
            if (! wasEnabled || captureBuf.getNumSamples() != holdSamples)
            {
                captureBuf.setSize (2, holdSamples);
                captureBuf.clear();
                capturePos = 0;
                capturing = true;
                readPos = 0.0f;
                speed = 1.0f;
            }
            wasEnabled = true;

            const float shrink = juce::jlimit (0.3f, 0.98f, speedShrinkPerLoop);
            constexpr float kMinSpeed = 0.04f; // floor so it crawls rather than hanging on one sample

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= holdSamples) { capturing = false; readPos = 0.0f; }
                }
                else
                {
                    const int idx = (int) readPos;
                    const int idxNext = (idx + 1) % holdSamples;
                    const float frac = readPos - (float) idx;
                    const float wetL = captureBuf.getSample (0, idx) * (1.0f - frac) + captureBuf.getSample (0, idxNext) * frac;
                    const float wetR = captureBuf.getSample (1, idx) * (1.0f - frac) + captureBuf.getSample (1, idxNext) * frac;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                    readPos += speed;
                    if (readPos >= (float) holdSamples)
                    {
                        readPos = std::fmod (readPos, (float) holdSamples);
                        speed = juce::jmax (kMinSpeed, speed * shrink);
                    }
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int capturePos = 0;
        float readPos = 0.0f, speed = 1.0f;
    };

    class RepeatPitchDown
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, float semitonesPerLoop, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f) { wasEnabled = false; return; }

            const int holdSamples = juce::jmax (64, (int) (holdMs * 0.001f * vlab::kSampleRate));
            if (! wasEnabled || captureBuf.getNumSamples() != holdSamples)
            {
                captureBuf.setSize (2, holdSamples);
                captureBuf.clear();
                capturePos = 0;
                capturing = true;
                readPos = 0.0f;
                pitchRatio = 1.0f;
                outputCount = 0;
            }
            wasEnabled = true;

            const float dropPerLoop = juce::jlimit (0.0f, 12.0f, semitonesPerLoop);
            constexpr float kMinPitchRatio = 0.06f; // roughly four octaves down, a floor not a limiter

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= holdSamples) { capturing = false; readPos = 0.0f; outputCount = 0; }
                }
                else
                {
                    const int idx = ((int) readPos) % holdSamples;
                    const int idxNext = (idx + 1) % holdSamples;
                    const float frac = readPos - std::floor (readPos);
                    const float wetL = captureBuf.getSample (0, idx) * (1.0f - frac) + captureBuf.getSample (0, idxNext) * frac;
                    const float wetR = captureBuf.getSample (1, idx) * (1.0f - frac) + captureBuf.getSample (1, idxNext) * frac;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                    readPos += pitchRatio;
                    if (readPos >= (float) holdSamples)
                        readPos = std::fmod (readPos, (float) holdSamples);

                    if (++outputCount >= holdSamples)
                    {
                        outputCount = 0;
                        readPos = 0.0f;
                        pitchRatio = juce::jmax (kMinPitchRatio, pitchRatio * std::pow (2.0f, -dropPerLoop / 12.0f));
                    }
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int capturePos = 0, outputCount = 0;
        float readPos = 0.0f, pitchRatio = 1.0f;
    };

    class BufferShuffle
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, int numChunks, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f) { wasEnabled = false; return; }

            const int holdSamples = juce::jmax (256, (int) (holdMs * 0.001f * vlab::kSampleRate));
            const int chunks = juce::jlimit (2, 16, numChunks);
            const int chunkLen = juce::jmax (32, holdSamples / chunks);
            const int totalLen = chunkLen * chunks;

            if (! wasEnabled || captureBuf.getNumSamples() != totalLen)
            {
                captureBuf.setSize (2, totalLen);
                captureBuf.clear();
                capturePos = 0;
                capturing = true;
                chunkOrder.clear();
            }
            wasEnabled = true;

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= totalLen)
                    {
                        capturing = false;
                        newShuffleOrder (chunks);
                        orderIdx = 0;
                        posInChunk = 0;
                    }
                }
                else
                {
                    const int chunkStart = chunkOrder[(size_t) orderIdx] * chunkLen;
                    const int idx = chunkStart + posInChunk;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + captureBuf.getSample (0, idx) * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + captureBuf.getSample (1, idx) * mix);

                    if (++posInChunk >= chunkLen)
                    {
                        posInChunk = 0;
                        if (++orderIdx >= (int) chunkOrder.size())
                        {
                            newShuffleOrder (chunks);
                            orderIdx = 0;
                        }
                    }
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int capturePos = 0, orderIdx = 0, posInChunk = 0;
        std::vector<int> chunkOrder;
        juce::Random rng;

        void newShuffleOrder (int chunks)
        {
            chunkOrder.resize ((size_t) chunks);
            for (int i = 0; i < chunks; ++i) chunkOrder[(size_t) i] = i;
            for (int i = chunks - 1; i > 0; --i)
            {
                const int j = rng.nextInt (i + 1);
                std::swap (chunkOrder[(size_t) i], chunkOrder[(size_t) j]);
            }
        }
    };

    class Scatter
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float grainMs, float mix, int numSamples)
        {
            const int ringLen = (int) vlab::kSampleRate; // 1s of rolling history
            if (ring.getNumSamples() != ringLen)
            {
                ring.setSize (2, ringLen);
                ring.clear();
                writePos = 0;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                ring.setSample (0, writePos, buf.getSample (0, i));
                ring.setSample (1, writePos, buf.getSample (1, i));
                writePos = (writePos + 1) % ringLen;
            }

            if (! enabled || mix <= 0.0f)
                return;

            const int grainLen = juce::jlimit (128, ringLen / 2, (int) (grainMs * 0.001f * vlab::kSampleRate));
            const int fadeLen = juce::jmax (8, juce::jmin (grainLen / 4, (int) (vlab::kSampleRate * 0.005)));

            if (currentGrainLen != grainLen)
            {
                currentGrainLen = grainLen;
                grainPos = currentGrainLen; // force picking a fresh grain below
            }

            for (int i = 0; i < numSamples; ++i)
            {
                if (grainPos >= currentGrainLen)
                {
                    const int maxLookback = juce::jmax (currentGrainLen * 2, ringLen - currentGrainLen - 1);
                    const int lookback = rng.nextInt (maxLookback) + currentGrainLen;
                    grainStart = (writePos - lookback + ringLen) % ringLen;
                    grainPos = 0;
                }

                const int readPos = (grainStart + grainPos) % ringLen;
                float env = 1.0f;
                if (grainPos < fadeLen) env = (float) grainPos / (float) fadeLen;
                else if (grainPos > currentGrainLen - fadeLen) env = (float) (currentGrainLen - grainPos) / (float) fadeLen;
                env = juce::jlimit (0.0f, 1.0f, env);

                const float wetL = ring.getSample (0, readPos) * env;
                const float wetR = ring.getSample (1, readPos) * env;
                buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                ++grainPos;
            }
        }

    private:
        vlab::AudioBuffer ring;
        int writePos = 0;
        int grainStart = 0, grainPos = 0, currentGrainLen = 0;
        juce::Random rng;
    };

    class ReverseBursts
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float probability, float burstMs, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                state = Idle;
                return;
            }

            const int burstSamples = juce::jmax (256, (int) (burstMs * 0.001f * vlab::kSampleRate));
            if (captureBuf.getNumSamples() != burstSamples)
            {
                captureBuf.setSize (2, burstSamples);
                captureBuf.clear();
                state = Idle;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                if (state == Idle && rng.nextFloat() < probability * 0.002f)
                {
                    state = Capturing;
                    pos = 0;
                }

                if (state == Capturing)
                {
                    captureBuf.setSample (0, pos, buf.getSample (0, i));
                    captureBuf.setSample (1, pos, buf.getSample (1, i));
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix));
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix));
                    if (++pos >= burstSamples) { state = Playing; pos = 0; }
                }
                else if (state == Playing)
                {
                    const int readPos = burstSamples - 1 - pos;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + captureBuf.getSample (0, readPos) * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + captureBuf.getSample (1, readPos) * mix);
                    if (++pos >= burstSamples) state = Idle;
                }
            }
        }

    private:
        enum State { Idle, Capturing, Playing };
        State state = Idle;
        vlab::AudioBuffer captureBuf;
        int pos = 0;
        juce::Random rng;
    };

    class GlitchGate
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float stepMs, float density, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
                return;

            const int stepSamples = juce::jmax (32, (int) (stepMs * 0.001f * vlab::kSampleRate));
            const float silenceChance = juce::jlimit (0.0f, 1.0f, density);

            for (int i = 0; i < numSamples; ++i)
            {
                if (stepCounter <= 0)
                {
                    stepCounter = stepSamples;
                    stepOpen = rng.nextFloat() >= silenceChance;
                }
                const float gateGain = stepOpen ? 1.0f : 0.0f;
                buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix * (1.0f - gateGain)));
                buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix * (1.0f - gateGain)));
                --stepCounter;
            }
        }

    private:
        int stepCounter = 0;
        bool stepOpen = true;
        juce::Random rng;
    };

    class BufferFreeze
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float grainMs, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                wasEnabled = false;
                return;
            }

            const int grainSamples = juce::jmax (64, (int) (grainMs * 0.001f * vlab::kSampleRate));
            if (! wasEnabled || captureBuf.getNumSamples() != grainSamples)
            {
                captureBuf.setSize (2, grainSamples);
                captureBuf.clear();
                pos = 0;
                capturing = true;
            }
            wasEnabled = true;

            for (int i = 0; i < numSamples; ++i)
            {
                if (capturing)
                {
                    captureBuf.setSample (0, pos, buf.getSample (0, i));
                    captureBuf.setSample (1, pos, buf.getSample (1, i));
                    if (++pos >= grainSamples) { capturing = false; pos = 0; }
                }
                else
                {
                    const float wetL = captureBuf.getSample (0, pos);
                    const float wetR = captureBuf.getSample (1, pos);
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);
                    pos = (pos + 1) % grainSamples;
                }
            }
        }

    private:
        vlab::AudioBuffer captureBuf;
        bool wasEnabled = false, capturing = false;
        int pos = 0;
    };

    class VinylStop
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float probability, float stopMs, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                state = Idle;
                return;
            }

            const int stopSamples = juce::jmax (256, (int) (stopMs * 0.001f * vlab::kSampleRate));
            const int captureSamples = juce::jmax (256, (int) (vlab::kSampleRate * 0.3)); // 300ms captured to spin down through
            if (captureBuf.getNumSamples() != captureSamples)
            {
                captureBuf.setSize (2, captureSamples);
                captureBuf.clear();
                state = Idle;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                if (state == Idle && rng.nextFloat() < probability * 0.002f)
                {
                    state = Capturing;
                    capturePos = 0;
                }

                if (state == Capturing)
                {
                    captureBuf.setSample (0, capturePos, buf.getSample (0, i));
                    captureBuf.setSample (1, capturePos, buf.getSample (1, i));
                    if (++capturePos >= captureSamples) { state = Stopping; readPos = 0.0f; stopStep = 0; }
                }
                else if (state == Stopping)
                {
                    const float t = (float) stopStep / (float) stopSamples;
                    const float speed = juce::jmax (0.0f, 1.0f - t);
                    const int idx = (int) readPos;

                    if (idx < captureSamples - 1)
                    {
                        const float frac = readPos - (float) idx;
                        const float wetL = captureBuf.getSample (0, idx) * (1.0f - frac) + captureBuf.getSample (0, idx + 1) * frac;
                        const float wetR = captureBuf.getSample (1, idx) * (1.0f - frac) + captureBuf.getSample (1, idx + 1) * frac;
                        buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                        buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);
                    }
                    else
                    {
                        buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix));
                        buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix));
                    }

                    readPos += speed;
                    ++stopStep;
                    if (stopStep >= stopSamples || speed <= 0.0f)
                        state = Idle;
                }
            }
        }

    private:
        enum State { Idle, Capturing, Stopping };
        State state = Idle;
        vlab::AudioBuffer captureBuf;
        int capturePos = 0, stopStep = 0;
        float readPos = 0.0f;
        juce::Random rng;
    };

    class BrokenDelay
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float baseDelayMs, float feedback, float jumpRate, float mix, int numSamples)
        {
            const int maxDelaySamples = (int) (2.0 * vlab::kSampleRate);
            if (delayBuf.getNumSamples() != maxDelaySamples)
            {
                delayBuf.setSize (2, maxDelaySamples);
                delayBuf.clear();
                writePos = 0;
                currentDelaySamples = juce::jlimit (1, maxDelaySamples - 1, (int) (baseDelayMs * 0.001f * vlab::kSampleRate));
            }

            if (! enabled || mix <= 0.0f)
                return;

            const float fb = juce::jlimit (0.0f, 0.95f, feedback);
            const float jumpChance = juce::jlimit (0.0f, 1.0f, jumpRate) * 0.002f;

            for (int i = 0; i < numSamples; ++i)
            {
                if (rng.nextFloat() < jumpChance)
                {
                    const int base = juce::jlimit (1, maxDelaySamples - 1, (int) (baseDelayMs * 0.001f * vlab::kSampleRate));
                    currentDelaySamples = juce::jlimit (1, maxDelaySamples - 1, base / 2 + rng.nextInt (base * 2));
                }

                const int readPos = (writePos - currentDelaySamples + maxDelaySamples) % maxDelaySamples;
                const float delayedL = delayBuf.getSample (0, readPos);
                const float delayedR = delayBuf.getSample (1, readPos);

                delayBuf.setSample (0, writePos, buf.getSample (0, i) + delayedL * fb);
                delayBuf.setSample (1, writePos, buf.getSample (1, i) + delayedR * fb);

                buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + delayedL * mix);
                buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + delayedR * mix);

                writePos = (writePos + 1) % maxDelaySamples;
            }
        }

    private:
        vlab::AudioBuffer delayBuf;
        int writePos = 0, currentDelaySamples = 0;
        juce::Random rng;
    };

    class PitchJumps
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float rate, float jumpMs, float maxSemitones, float mix, int numSamples)
        {
            const int ringLen = (int) vlab::kSampleRate;
            if (ring.getNumSamples() != ringLen)
            {
                ring.setSize (2, ringLen);
                ring.clear();
                writePos = 0;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                ring.setSample (0, writePos, buf.getSample (0, i));
                ring.setSample (1, writePos, buf.getSample (1, i));
                writePos = (writePos + 1) % ringLen;
            }

            if (! enabled || mix <= 0.0f)
            {
                active = false;
                return;
            }

            const int jumpSamples = juce::jmax (256, (int) (jumpMs * 0.001f * vlab::kSampleRate));

            for (int i = 0; i < numSamples; ++i)
            {
                if (! active && rng.nextFloat() < juce::jmax (0.0f, rate) * 0.002f)
                {
                    active = true;
                    activeSamplesLeft = jumpSamples;
                    const float semis = (rng.nextFloat() * 2.0f - 1.0f) * juce::jmax (0.0f, maxSemitones);
                    pitchRatio = std::pow (2.0f, semis / 12.0f);
                    readPos = (float) ((writePos - jumpSamples * 2 + ringLen) % ringLen);
                }

                if (active)
                {
                    const int idx = ((int) readPos) % ringLen;
                    const int idxNext = (idx + 1) % ringLen;
                    const float frac = readPos - std::floor (readPos);
                    const float wetL = ring.getSample (0, idx) * (1.0f - frac) + ring.getSample (0, idxNext) * frac;
                    const float wetR = ring.getSample (1, idx) * (1.0f - frac) + ring.getSample (1, idxNext) * frac;
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                    readPos += pitchRatio;
                    if (readPos >= (float) ringLen) readPos -= (float) ringLen;
                    if (--activeSamplesLeft <= 0) active = false;
                }
            }
        }

    private:
        vlab::AudioBuffer ring;
        int writePos = 0;
        bool active = false;
        int activeSamplesLeft = 0;
        float readPos = 0.0f, pitchRatio = 1.0f;
        juce::Random rng;
    };

    // Plays consecutive segments of recent audio alternately forward and backward.
    class Backmask
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float segmentMs, float mix, int numSamples)
        {
            const int ringLen = (int) vlab::kSampleRate;
            if (ring.getNumSamples() != ringLen)
            {
                ring.setSize (2, ringLen);
                ring.clear();
                writePos = 0;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                ring.setSample (0, writePos, buf.getSample (0, i));
                ring.setSample (1, writePos, buf.getSample (1, i));
                writePos = (writePos + 1) % ringLen;
            }

            if (! enabled || mix <= 0.0f)
            {
                segPos = 0;
                startOfSegment = true;
                return;
            }

            const int segSamples = juce::jmax (256, (int) (segmentMs * 0.001f * vlab::kSampleRate));
            if (segLen != segSamples)
            {
                segLen = segSamples;
                segPos = 0;
                startOfSegment = true;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                if (startOfSegment)
                {
                    segStart = (writePos - segLen * 2 + ringLen) % ringLen;
                    startOfSegment = false;
                }

                const int offset = reversedNow ? (segLen - 1 - segPos) : segPos;
                const int idx = (segStart + offset) % ringLen;
                const float wetL = ring.getSample (0, idx);
                const float wetR = ring.getSample (1, idx);
                buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + wetL * mix);
                buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + wetR * mix);

                if (++segPos >= segLen)
                {
                    segPos = 0;
                    startOfSegment = true;
                    reversedNow = ! reversedNow;
                }
            }
        }

    private:
        vlab::AudioBuffer ring;
        int writePos = 0;
        int segLen = 0, segPos = 0, segStart = 0;
        bool reversedNow = false, startOfSegment = true;
    };

    class MicroFreeze
    {
    public:
        void process (vlab::AudioBuffer& buf, bool enabled, float holdMs, float density, float mix, int numSamples)
        {
            if (! enabled || mix <= 0.0f)
            {
                counter = 0;
                return;
            }

            const int holdLen = juce::jmax (2, (int) (holdMs * 0.001f * vlab::kSampleRate));
            const float chance = juce::jlimit (0.0f, 1.0f, density);

            for (int i = 0; i < numSamples; ++i)
            {
                if (counter <= 0)
                {
                    freezing = rng.nextFloat() < chance;
                    counter = holdLen;
                    if (freezing)
                    {
                        heldL = buf.getSample (0, i);
                        heldR = buf.getSample (1, i);
                    }
                }

                if (freezing)
                {
                    buf.setSample (0, i, buf.getSample (0, i) * (1.0f - mix) + heldL * mix);
                    buf.setSample (1, i, buf.getSample (1, i) * (1.0f - mix) + heldR * mix);
                }

                --counter;
            }
        }

    private:
        int counter = 0;
        bool freezing = false;
        float heldL = 0.0f, heldR = 0.0f;
        juce::Random rng;
    };
}
