#include "../SDK/VLabModAPI.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace
{
    constexpr int kNumBands = 8;
    constexpr float kBandBaseHz[kNumBands] = { 180.0f, 300.0f, 500.0f, 850.0f, 1400.0f, 2300.0f, 3800.0f, 6000.0f };
    constexpr float kBandQ = 4.0f;

    constexpr int kNumParams = 6;
    enum ParamIndex { kCarrierFreqHz = 0, kFormantShift, kBandFollowMs, kNoiseAmount, kMix, kOutputGainDb };

    struct ParamRange { const char* name; float minV, maxV, defaultV; };

    constexpr ParamRange kParamRanges[kNumParams] = {
        { "Carrier Freq (Hz)",  40.0f, 400.0f, 110.0f }, // buzz pitch of the internal carrier
        { "Formant Shift",      -1.0f,   1.0f,   0.0f }, // shifts band centers, changes perceived voice size
        { "Band Speed (ms)",     1.0f,  50.0f,   8.0f }, // envelope follower attack/release per band
        { "Noise Amount",        0.0f,   1.0f,  0.15f }, // blends noise into the carrier for sibilance
        { "Mix",                 0.0f,   1.0f,   0.8f },
        { "Output Gain (dB)",  -24.0f,  24.0f,   0.0f }
    };

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    // Simple RBJ constant-skirt-gain bandpass biquad, direct form 1.
    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        void setBandpass (float freqHz, float q, double sampleRate)
        {
            freqHz = std::clamp (freqHz, 20.0f, (float) (sampleRate * 0.45));
            const float w0 = 2.0f * (float) M_PI * freqHz / (float) sampleRate;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float cosw0 = std::cos (w0);

            const float a0 = 1.0f + alpha;
            b0 = alpha / a0;
            b1 = 0.0f;
            b2 = -alpha / a0;
            a1 = (-2.0f * cosw0) / a0;
            a2 = (1.0f - alpha) / a0;
        }

        inline float process (float x)
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    struct RoboVoxState
    {
        double sampleRate = 44100.0;
        float params[kNumParams];

        Biquad modulatorBand[kNumBands]; // filters the dry input into each band
        Biquad carrierBand[kNumBands];   // filters the internal carrier into each band
        float envelope[kNumBands] = { 0 };

        float carrierPhase = 0.0f;
        unsigned int noiseSeed = 0x1234567u;

        float lastFormantShift = 0.0f; // so we only recompute biquad coeffs when it actually changes

        RoboVoxState()
        {
            for (int i = 0; i < kNumParams; ++i)
                params[i] = kParamRanges[i].defaultV;
        }

        float nextNoise()
        {
            // xorshift32, fast, deterministic, no <random> allocation churn per sample
            noiseSeed ^= noiseSeed << 13;
            noiseSeed ^= noiseSeed >> 17;
            noiseSeed ^= noiseSeed << 5;
            return ((float) (noiseSeed & 0xFFFFFF) / (float) 0xFFFFFF) * 2.0f - 1.0f;
        }

        void updateBandFilters (float formantShift)
        {
            const float shiftMul = std::pow (2.0f, formantShift * 0.5f); // +-1 => +-1 octave-ish max
            for (int i = 0; i < kNumBands; ++i)
            {
                const float f = kBandBaseHz[i] * shiftMul;
                modulatorBand[i].setBandpass (f, kBandQ, sampleRate);
                carrierBand[i].setBandpass (f, kBandQ, sampleRate);
            }
            lastFormantShift = formantShift;
        }
    };

    RoboVoxState state;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info (VLabModInfo* info)
{
    std::strncpy (info->name, "RoboVox", sizeof (info->name));
    std::strncpy (info->author, "vlab", sizeof (info->author));
    std::strncpy (info->version, "1.0", sizeof (info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}

extern "C" VLAB_MOD_EXPORT bool vlab_mod_init (double sampleRate, int /*maxBlockSize*/)
{
    state = RoboVoxState();
    state.sampleRate = sampleRate;
    state.updateBandFilters (state.params[kFormantShift]);
    return true;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_process (float* interleaved, int numFrames, int numChannels)
{
    if (numChannels <= 0)
        return;

    const float carrierFreq  = state.params[kCarrierFreqHz];
    const float formantShift = state.params[kFormantShift];
    const float followMs     = std::max (1.0f, state.params[kBandFollowMs]);
    const float noiseAmount  = std::clamp (state.params[kNoiseAmount], 0.0f, 1.0f);
    const float mix          = std::clamp (state.params[kMix], 0.0f, 1.0f);
    const float outputGain   = dbToGain (state.params[kOutputGainDb]);

    if (std::fabs (formantShift - state.lastFormantShift) > 1.0e-4f)
        state.updateBandFilters (formantShift);

    const float followCoeff = std::exp (-1.0f / ((followMs / 1000.0f) * (float) state.sampleRate));
    const float carrierPhaseInc = 2.0f * (float) M_PI * carrierFreq / (float) state.sampleRate;

    for (int frame = 0; frame < numFrames; ++frame)
    {
        // mono modulator: average whatever channels are present
        float modulator = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            modulator += interleaved[frame * numChannels + ch];
        modulator /= (float) numChannels;

        state.carrierPhase += carrierPhaseInc;
        if (state.carrierPhase > (float) M_PI) state.carrierPhase -= 2.0f * (float) M_PI;
        const float sawCarrier = state.carrierPhase / (float) M_PI; // -1..1 ramp
        const float carrierRaw = sawCarrier * (1.0f - noiseAmount) + state.nextNoise() * noiseAmount;

        float vocoded = 0.0f;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float modBand = state.modulatorBand[b].process (modulator);
            const float carBand = state.carrierBand[b].process (carrierRaw);

            const float rectified = std::fabs (modBand);
            state.envelope[b] = state.envelope[b] * followCoeff + rectified * (1.0f - followCoeff);

            vocoded += carBand * state.envelope[b] * 3.0f; // makeup for bandpass energy loss
        }

        vocoded *= outputGain;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = interleaved[frame * numChannels + ch];
            interleaved[frame * numChannels + ch] = dry * (1.0f - mix) + vocoded * mix;
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
