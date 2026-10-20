#pragma once
#include <atomic>
#include <string>
#include <mutex>

namespace vlab
{
    enum class WaveType { Sine, Square, Saw, Triangle, Pulse, SuperSaw, Organ, Wavefolder };
    enum class ModTarget { None_, Pitch, Amp, Filter, Osc2Mix };
    enum class ArpPattern { Up, Down, UpDown, Random };
    enum class FilterType { LowPass, HighPass, BandPass, Notch };
    enum class LfoShape { Sine, Triangle, Square, SampleHold };

    struct SynthParams
    {
        std::atomic<float> freq { 440.0f }, detune { 0.0f }, portamento { 0.0f };
        std::atomic<float> arpBpm { 120.0f };
        std::atomic<bool>  arpOn { false };
        std::atomic<float> arpOffsetSemitones { 0.0f }; // "_arp_offset"
        std::atomic<ArpPattern> arpPattern { ArpPattern::Up };
        std::atomic<int>   arpOctaveRange { 1 };          // 1-3 octaves the pattern spans
        std::atomic<float> arpDivisionBeats { 0.5f };      // step length in beats (0.5 == eighth notes)

        std::atomic<float> modLfoRate { 5.0f }, modLfoDepth { 0.0f };
        std::atomic<ModTarget> modTarget { ModTarget::None_ };
        std::atomic<LfoShape> modLfoShape { LfoShape::Sine };
        std::atomic<float> modPan { 0.0f };

        std::atomic<int>   unison { 1 };
        std::atomic<float> unisonDetune { 10.0f };
        std::atomic<WaveType> wave { WaveType::Sine };

        std::atomic<float> fmDepth { 0.0f }, fmFreq { 440.0f };

        std::atomic<float> osc2Mix { 0.0f }, osc2Detune { 700.0f };
        std::atomic<WaveType> osc2Wave { WaveType::Saw };

        std::atomic<float> subMix { 0.0f }, noiseMix { 0.0f };

        std::atomic<float> envA { 0.05f }, envD { 0.2f }, envS { 0.7f }, envR { 0.3f };

        std::atomic<float> filterCutoff { 1.0f };
        std::atomic<FilterType> filterType { FilterType::LowPass };
        std::atomic<float> filterResonance { 0.0f };
        std::atomic<float> filterEnvAmount { 0.0f };

        std::atomic<float> velocitySensitivity { 0.0f }; // 0 == every note-on is full velocity

        std::atomic<float> drive { 0.0f };      // pre-filter saturation
        std::atomic<float> fxDist { 0.0f };
        std::atomic<float> fxBitcrush { 0.0f };

        std::atomic<float> chorusRate { 0.8f }, chorusDepth { 0.0f }, chorusMix { 0.0f };
        std::atomic<float> delayTimeMs { 280.0f }, delayFeedback { 0.3f }, delayMix { 0.0f };

        std::atomic<float> masterVol { 0.5f };

        void resetToDefaults()
        {
            freq = 440.0f; detune = 0.0f; portamento = 0.0f;
            arpBpm = 120.0f; arpOn = false; arpOffsetSemitones = 0.0f;
            arpPattern = ArpPattern::Up; arpOctaveRange = 1; arpDivisionBeats = 0.5f;

            modLfoRate = 5.0f; modLfoDepth = 0.0f;
            modTarget = ModTarget::None_; modLfoShape = LfoShape::Sine; modPan = 0.0f;

            unison = 1; unisonDetune = 10.0f; wave = WaveType::Sine;
            fmDepth = 0.0f; fmFreq = 440.0f;

            osc2Mix = 0.0f; osc2Detune = 700.0f; osc2Wave = WaveType::Saw;
            subMix = 0.0f; noiseMix = 0.0f;

            envA = 0.05f; envD = 0.2f; envS = 0.7f; envR = 0.3f;

            filterCutoff = 1.0f; filterType = FilterType::LowPass;
            filterResonance = 0.0f; filterEnvAmount = 0.0f;

            velocitySensitivity = 0.0f;

            drive = 0.0f; fxDist = 0.0f; fxBitcrush = 0.0f;

            chorusRate = 0.8f; chorusDepth = 0.0f; chorusMix = 0.0f;
            delayTimeMs = 280.0f; delayFeedback = 0.3f; delayMix = 0.0f;

            masterVol = 0.5f;
        }
    };
}
