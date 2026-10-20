#include "../SDK/VLabModAPI.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace
{
    constexpr int kNumParams = 4;
    enum ParamIndex { kSwellLengthMs = 0, kIntervalMs, kMix, kFeedback };

    struct ParamRange { const char* name; float minV, maxV, defaultV; };

    constexpr ParamRange kParamRanges[kNumParams] = {
        { "Swell Length (ms)",    200.0f, 3000.0f, 900.0f  }, // captured window length that gets reversed
        { "Trigger Interval (ms)", 500.0f, 8000.0f, 3000.0f }, // how often a new swell fires
        { "Mix",                    0.0f,    1.0f,   0.35f }, // wet blend against dry
        { "Feedback",               0.0f,    0.9f,   0.2f  }  // feeds the reversed buffer back into itself
    };

    constexpr int kMaxChannels = 2;
    constexpr double kMaxCaptureSeconds = 3.5; // covers the max Swell Length with headroom

    struct ReverseSwellState
    {
        double sampleRate = 44100.0;
        float params[kNumParams];

        // rolling capture buffer per channel (dry input, always recording)
        std::vector<float> captureBuf[kMaxChannels];
        int captureLen = 0;
        int writePos = 0;

        // active swell playback state
        bool swellActive = false;
        std::vector<float> swellBuf[kMaxChannels]; // reversed snapshot being played
        int swellLen = 0;
        int swellPos = 0;

        int samplesSinceLastTrigger = 0;
        int nextTriggerSamples = 0;

        ReverseSwellState()
        {
            for (int i = 0; i < kNumParams; ++i)
                params[i] = kParamRanges[i].defaultV;
        }
    };

    ReverseSwellState state;

    void triggerSwell (int numChannels)
    {
        const int swellLenSamples = std::clamp ((int) (state.params[kSwellLengthMs] * 0.001 * state.sampleRate),
                                                  1, state.captureLen);
        state.swellLen = swellLenSamples;
        state.swellPos = 0;

        for (int ch = 0; ch < std::min (numChannels, kMaxChannels); ++ch)
        {
            state.swellBuf[ch].resize ((size_t) swellLenSamples);
            for (int i = 0; i < swellLenSamples; ++i)
            {
                const int sourceIndex = ((state.writePos - 1 - i) % state.captureLen + state.captureLen) % state.captureLen;
                state.swellBuf[ch][(size_t) i] = state.captureBuf[ch][(size_t) sourceIndex];
            }
        }
        state.swellActive = true;
    }
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info (VLabModInfo* info)
{
    std::strncpy (info->name, "Reverse Swell", sizeof (info->name));
    std::strncpy (info->author, "vlab", sizeof (info->author));
    std::strncpy (info->version, "1.0", sizeof (info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}

extern "C" VLAB_MOD_EXPORT bool vlab_mod_init (double sampleRate, int /*maxBlockSize*/)
{
    state = ReverseSwellState();
    state.sampleRate = sampleRate;
    state.captureLen = std::max (1, (int) (kMaxCaptureSeconds * sampleRate));
    for (auto& buf : state.captureBuf)
        buf.assign ((size_t) state.captureLen, 0.0f);
    state.nextTriggerSamples = (int) (state.params[kIntervalMs] * 0.001 * sampleRate);
    return true;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_process (float* interleaved, int numFrames, int numChannels)
{
    if (numChannels <= 0 || state.captureLen <= 0)
        return;

    const float mix      = std::clamp (state.params[kMix], 0.0f, 1.0f);
    const float feedback = std::clamp (state.params[kFeedback], 0.0f, 0.9f);
    const int chCount = std::min (numChannels, kMaxChannels);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        for (int ch = 0; ch < chCount; ++ch)
            state.captureBuf[ch][(size_t) state.writePos] = interleaved[frame * numChannels + ch];
        state.writePos = (state.writePos + 1) % state.captureLen;

        // 2) periodic trigger
        ++state.samplesSinceLastTrigger;
        if (state.samplesSinceLastTrigger >= state.nextTriggerSamples)
        {
            state.samplesSinceLastTrigger = 0;
            state.nextTriggerSamples = std::max (1, (int) (state.params[kIntervalMs] * 0.001 * state.sampleRate));
            triggerSwell (numChannels);
        }

        if (state.swellActive)
        {
            const float swellProgress = state.swellLen > 0 ? (float) state.swellPos / (float) state.swellLen : 1.0f;
            const float envelope = std::sin (swellProgress * (float) M_PI); // rises then falls back to 0, one smooth swell

            for (int ch = 0; ch < chCount; ++ch)
            {
                const float wet = state.swellBuf[ch][(size_t) state.swellPos] * envelope;
                const float dry = interleaved[frame * numChannels + ch];
                interleaved[frame * numChannels + ch] = dry * (1.0f - mix) + wet * mix;

                if (feedback > 0.0f)
                {
                    const int smearTarget = (state.swellPos + state.swellLen / 4) % std::max (1, state.swellLen);
                    state.swellBuf[ch][(size_t) smearTarget] += wet * feedback * 0.5f;
                }
            }

            ++state.swellPos;
            if (state.swellPos >= state.swellLen)
                state.swellActive = false;
        }
    }
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_set_param (int index, float value)
{
    if (index >= 0 && index < kNumParams)
        state.params[index] = std::clamp (value, kParamRanges[index].minV, kParamRanges[index].maxV);
}

extern "C" VLAB_MOD_EXPORT float vlab_mod_get_param (int index)
{
    if (index >= 0 && index < kNumParams)
        return state.params[index];
    return 0.0f;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_shutdown()
{
}

extern "C" VLAB_MOD_EXPORT int vlab_mod_get_param_count()
{
    return kNumParams;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_param_info (int index, VLabModParamInfo* outInfo)
{
    if (index < 0 || index >= kNumParams)
        return;
    const auto& range = kParamRanges[index];
    std::strncpy (outInfo->name, range.name, sizeof (outInfo->name));
    outInfo->minValue = range.minV;
    outInfo->maxValue = range.maxV;
    outInfo->defaultValue = range.defaultV;
}
