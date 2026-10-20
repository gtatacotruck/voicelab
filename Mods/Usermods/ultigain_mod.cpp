#include "../SDK/VLabModAPI.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace
{
    constexpr int kNumParams = 8;
    enum ParamIndex
    {
        kInputGainDb = 0,
        kDrive,
        kToneTilt,
        kGateThresholdDb,
        kLimiterCeilingDb,
        kLimiterReleaseMs,
        kOutputGainDb,
        kMix
    };

    struct ParamRange { const char* name; float minV, maxV, defaultV; };

    constexpr ParamRange kParamRanges[kNumParams] = {
        { "Input Gain (dB)",     -24.0f, 48.0f, 18.0f  }, // pushed hot by default, this mod's whole point
        { "Drive",                 0.0f,  1.0f,  0.6f  }, // soft-clip saturation amount
        { "Tone Tilt",             -1.0f,  1.0f,  0.2f  }, // -1 = darker/bassier, +1 = brighter/thinner
        { "Gate Threshold (dB)", -80.0f,  0.0f,-50.0f  }, // silence below this is ducked out
        { "Limiter Ceiling (dB)", -6.0f,  0.0f, -0.3f  }, // brick-wall peak ceiling
        { "Limiter Release (ms)",  5.0f,500.0f, 60.0f  },
        { "Output Gain (dB)",    -24.0f, 24.0f,  0.0f  },
        { "Mix",                   0.0f,  1.0f,  1.0f  }
    };

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    struct UltiGainState
    {
        double sampleRate = 44100.0;
        float params[kNumParams];

        // one-pole tilt filter state (per channel)
        float tiltLowState[2] = { 0.0f, 0.0f };

        float gateEnv = 0.0f;

        float limiterGainReduction = 1.0f;

        UltiGainState()
        {
            for (int i = 0; i < kNumParams; ++i)
                params[i] = kParamRanges[i].defaultV;
        }
    };

    UltiGainState state;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info (VLabModInfo* info)
{
    std::strncpy (info->name, "UltiGain", sizeof (info->name));
    std::strncpy (info->author, "vlab", sizeof (info->author));
    std::strncpy (info->version, "1.0", sizeof (info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}

extern "C" VLAB_MOD_EXPORT bool vlab_mod_init (double sampleRate, int /*maxBlockSize*/)
{
    state = UltiGainState();
    state.sampleRate = sampleRate;
    return true;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_process (float* interleaved, int numFrames, int numChannels)
{
    if (numChannels <= 0)
        return;

    const float inputGain   = dbToGain (state.params[kInputGainDb]);
    const float drive       = std::clamp (state.params[kDrive], 0.0f, 1.0f);
    const float tilt        = std::clamp (state.params[kToneTilt], -1.0f, 1.0f);
    const float gateThresh  = dbToGain (state.params[kGateThresholdDb]);
    const float ceiling     = dbToGain (state.params[kLimiterCeilingDb]);
    const float releaseMs   = std::max (5.0f, state.params[kLimiterReleaseMs]);
    const float outputGain  = dbToGain (state.params[kOutputGainDb]);
    const float mix         = std::clamp (state.params[kMix], 0.0f, 1.0f);

    const float tiltCutoffHz = 600.0f;
    const float tiltCoeff = std::exp (-2.0f * (float) M_PI * tiltCutoffHz / (float) state.sampleRate);

    const float gateAttackCoeff  = std::exp (-1.0f / (0.005f * (float) state.sampleRate));
    const float gateReleaseCoeff = std::exp (-1.0f / (0.15f  * (float) state.sampleRate));

    const float limiterAttackCoeff  = std::exp (-1.0f / (0.001f * (float) state.sampleRate));           // ~1ms, fast
    const float limiterReleaseCoeff = std::exp (-1.0f / ((releaseMs / 1000.0f) * (float) state.sampleRate));

    for (int frame = 0; frame < numFrames; ++frame)
    {
        float dryL = interleaved[frame * numChannels + 0];
        float dryR = numChannels > 1 ? interleaved[frame * numChannels + 1] : dryL;

        float peakAbs = 0.0f;
        float wetSamples[2];

        for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        {
            float x = interleaved[frame * numChannels + ch];

            // 1) input trim
            x *= inputGain;

            float& lowState = state.tiltLowState[ch];
            lowState = lowState * tiltCoeff + x * (1.0f - tiltCoeff);
            float low  = lowState;
            float high = x - low;
            const float lowGain  = 1.0f - std::max (0.0f, tilt) * 0.6f + std::max (0.0f, -tilt) * 0.6f;
            const float highGain = 1.0f + std::max (0.0f, tilt) * 0.6f - std::max (0.0f, -tilt) * 0.6f;
            x = low * lowGain + high * highGain;

            const float driveAmount = 1.0f + drive * 4.0f;
            const float driven = std::tanh (x * driveAmount) / std::tanh (std::max (driveAmount, 1.0001f));
            x = x * (1.0f - drive) + driven * drive;

            wetSamples[ch] = x;
            peakAbs = std::max (peakAbs, std::fabs (x));
        }

        const float targetGate = peakAbs;
        const float gateCoeff = targetGate > state.gateEnv ? gateAttackCoeff : gateReleaseCoeff;
        state.gateEnv = state.gateEnv * gateCoeff + targetGate * (1.0f - gateCoeff);
        const float gateGain = state.gateEnv > gateThresh ? 1.0f
                                                           : std::max (0.0f, state.gateEnv / std::max (gateThresh, 1.0e-6f));

        const float postGatePeak = peakAbs * gateGain;
        float desiredReduction = 1.0f;
        if (postGatePeak > ceiling && postGatePeak > 1.0e-6f)
            desiredReduction = ceiling / postGatePeak;

        const float limCoeff = desiredReduction < state.limiterGainReduction ? limiterAttackCoeff : limiterReleaseCoeff;
        state.limiterGainReduction = state.limiterGainReduction * limCoeff + desiredReduction * (1.0f - limCoeff);
        state.limiterGainReduction = std::min (state.limiterGainReduction, 1.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = ch == 0 ? dryL : dryR;
            float wet = (ch < 2 ? wetSamples[ch] : wetSamples[0]) * gateGain * state.limiterGainReduction * outputGain;
            interleaved[frame * numChannels + ch] = dry * (1.0f - mix) + wet * mix;
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
