#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace vlab
{
    constexpr double kSampleRate = 44100.0;
    constexpr int    kChannels   = 2;

    using AudioBuffer = juce::AudioBuffer<float>;
}
