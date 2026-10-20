#pragma once
#include <atomic>
#include <new>

namespace vlab
{
    struct AudioEngineParams
    {
        std::atomic<float> gain { 0.0f }, pitch { 0.0f }, pan { 0.0f };
        std::atomic<float> masterOutGain { 0.0f }, noiseGate { 0.0f };

        std::atomic<float> sbSpeed { 1.0f }, sbVol { 1.0f };
        std::atomic<bool>  sbLoop { false }, sbReverse { false };
        std::atomic<float> sbReverb { 0.0f };

        std::atomic<float> distortion { 0.0f };

        std::atomic<float> chorusRate { 1.0f }, chorusDepth { 0.5f }, chorusMix { 0.0f };
        std::atomic<float> phaserRate { 1.0f }, phaserDepth { 0.5f }, phaserMix { 0.0f };
        std::atomic<float> tremoloRate { 5.0f }, tremoloDepth { 0.0f };
        std::atomic<float> delayTime { 0.3f }, delay2Time { 0.6f };
        std::atomic<float> delayFeedback { 0.3f }, delayMix { 0.0f };
        std::atomic<float> reverbMix { 0.0f }, reverbDecay { 0.5f };

        std::atomic<float> compThreshold { 0.7f }, compRatio { 4.0f };
        std::atomic<float> compAttack { 0.01f }, compRelease { 0.1f };

        std::atomic<float> eqLo { 0.0f }, eqMid { 0.0f }, eqHi { 0.0f };
        std::atomic<float> bitcrush { 0.0f };

        std::atomic<bool>  loudMicBypass { false };
        std::atomic<float> limiterThreshold { 0.95f }, preLimiterGain { 1.0f };
        std::atomic<float> postLimiterGain { 1.0f }, adaptiveHeadroom { 0.05f };

        std::atomic<float> midiVol { 0.8f };

        std::atomic<bool>  headSpaceEnabled { false };
        std::atomic<float> headSpaceAzimuth { 0.0f }; // degrees, -180..180
        std::atomic<float> headSpaceWidth { 0.8f };   // 0..1 effect strength

        // Experimental
        std::atomic<bool>  expDeEsserEnabled { false };
        std::atomic<float> expDeEsserAmount { 0.5f };
        std::atomic<bool>  expAutoLevelEnabled { false };
        std::atomic<float> expAutoLevelAmount { 0.7f };
        std::atomic<bool>  expUnderwaterEnabled { false };
        std::atomic<float> expUnderwaterAmount { 0.7f };
        std::atomic<bool>  expPresenceEnabled { false };
        std::atomic<float> expPresenceAmount { 0.5f };
        std::atomic<bool>  expRadioSweepEnabled { false };
        std::atomic<float> expRadioSweepRate { 0.3f }, expRadioSweepAmount { 0.7f };
        std::atomic<bool>  expPumpEnabled { false };
        std::atomic<float> expPumpRate { 2.0f }, expPumpDepth { 0.6f };

        // Glitch FX (new, no Python equivalent)
        std::atomic<bool>  stutterEnabled { false };
        std::atomic<int>   stutterType { 0 }; // see vlab::dsp::StutterType
        std::atomic<float> stutterRate { 1.0f }; // roughly triggers/sec, average
        std::atomic<float> stutterHoldMs { 120.0f };
        std::atomic<float> stutterRandomness { 0.5f };
        std::atomic<float> stutterMix { 0.0f };
        std::atomic<float> ringModFreq { 30.0f };
        std::atomic<float> ringModMix { 0.0f };

        std::atomic<bool>  escRepeatEnabled { false };
        std::atomic<float> escRepeatHoldMs { 150.0f };
        std::atomic<float> escRepeatGrowth { 1.3f };
        std::atomic<float> escRepeatMix { 0.0f };

        std::atomic<bool>  accRepeatEnabled { false };
        std::atomic<float> accRepeatHoldMs { 200.0f };
        std::atomic<float> accRepeatSpeedGrowth { 1.3f };
        std::atomic<float> accRepeatMix { 0.0f };

        std::atomic<bool>  decRepeatEnabled { false };
        std::atomic<float> decRepeatHoldMs { 200.0f };
        std::atomic<float> decRepeatSpeedShrink { 0.8f };
        std::atomic<float> decRepeatMix { 0.0f };

        std::atomic<bool>  pitchDownRepeatEnabled { false };
        std::atomic<float> pitchDownRepeatHoldMs { 200.0f };
        std::atomic<float> pitchDownRepeatSemitones { 2.0f };
        std::atomic<float> pitchDownRepeatMix { 0.0f };

        std::atomic<bool>  scatterEnabled { false };
        std::atomic<float> scatterGrainMs { 60.0f };
        std::atomic<float> scatterMix { 0.0f };

        // Reverse Bursts: short random bursts, captured then played backwards.
        std::atomic<bool>  reverseBurstEnabled { false };
        std::atomic<float> reverseBurstProbability { 0.5f };
        std::atomic<float> reverseBurstMs { 200.0f };
        std::atomic<float> reverseBurstMix { 0.0f };

        // Glitch Gate: chops audio into steps, randomly mutes some hard.
        std::atomic<bool>  glitchGateEnabled { false };
        std::atomic<float> glitchGateStepMs { 80.0f };
        std::atomic<float> glitchGateDensity { 0.4f };
        std::atomic<float> glitchGateMix { 0.0f };

        // Buffer Freeze: loops a single grain infinitely while enabled.
        std::atomic<bool>  freezeEnabled { false };
        std::atomic<float> freezeGrainMs { 150.0f };
        std::atomic<float> freezeMix { 1.0f };

        std::atomic<bool>  vinylStopEnabled { false };
        std::atomic<float> vinylStopProbability { 0.3f };
        std::atomic<float> vinylStopMs { 500.0f };
        std::atomic<float> vinylStopMix { 0.0f };

        // Broken Delay: a delay whose tap time randomly jumps around.
        std::atomic<bool>  brokenDelayEnabled { false };
        std::atomic<float> brokenDelayMs { 300.0f };
        std::atomic<float> brokenDelayFeedback { 0.35f };
        std::atomic<float> brokenDelayJumpRate { 1.0f };
        std::atomic<float> brokenDelayMix { 0.0f };

        // Pitch Jumps: brief, random, stepped pitch shifts.
        std::atomic<bool>  pitchJumpEnabled { false };
        std::atomic<float> pitchJumpRate { 1.0f };
        std::atomic<float> pitchJumpMs { 150.0f };
        std::atomic<float> pitchJumpSemitones { 12.0f };
        std::atomic<float> pitchJumpMix { 0.0f };

        // Backmask: continuously alternating forward/reverse segments.
        std::atomic<bool>  backmaskEnabled { false };
        std::atomic<float> backmaskSegmentMs { 200.0f };
        std::atomic<float> backmaskMix { 0.0f };

        // Micro Freeze: rapid continuous tiny sample-and-hold texture.
        std::atomic<bool>  microFreezeEnabled { false };
        std::atomic<float> microFreezeHoldMs { 8.0f };
        std::atomic<float> microFreezeDensity { 0.4f };
        std::atomic<float> microFreezeMix { 0.0f };

        std::atomic<bool>  bufferShuffleEnabled { false };
        std::atomic<float> bufferShuffleHoldMs { 500.0f };
        std::atomic<int>   bufferShuffleChunks { 8 };
        std::atomic<float> bufferShuffleMix { 0.0f };

        // Vanilla FX (new, no Python equivalent)
        std::atomic<float> flangerRate { 0.3f }, flangerDepth { 0.5f }, flangerFeedback { 0.3f }, flangerMix { 0.0f };
        std::atomic<float> autoWahSensitivity { 1.0f }, autoWahRate { 0.5f }, autoWahDepth { 0.3f }, autoWahMix { 0.0f };
        std::atomic<float> vibratoRate { 5.0f }, vibratoDepth { 0.0f };
        std::atomic<float> autoPanRate { 0.5f }, autoPanDepth { 0.0f };
        std::atomic<float> telephoneMix { 0.0f };
        std::atomic<float> doublerDetuneCents { 12.0f }, doublerDelayMs { 18.0f }, doublerMix { 0.0f };

        std::atomic<float> browserAudioVol { 0.8f };

        void resetToDefaults() { new (this) AudioEngineParams(); }
    };
}
