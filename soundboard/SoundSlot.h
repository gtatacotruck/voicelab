#pragma once
#include "../DSP/Common.h"
#include <memory>
#include <string>
#include <vector>

namespace vlab
{
    enum class PlayMode { Overlap, Retrigger, StopOthers };

    inline const char* playModeToString (PlayMode m)
    {
        switch (m)
        {
            case PlayMode::Retrigger:  return "retrigger";
            case PlayMode::StopOthers: return "stop_others";
            case PlayMode::Overlap:    default: return "overlap";
        }
    }

    inline PlayMode playModeFromString (const juce::String& s)
    {
        if (s == "retrigger")   return PlayMode::Retrigger;
        if (s == "stop_others") return PlayMode::StopOthers;
        return PlayMode::Overlap;
    }

    struct SoundSlot
    {
        std::shared_ptr<vlab::AudioBuffer> data; // null == "Empty" slot
        std::string name = "Empty";
        std::string path;
        std::string hotkey;            // e.g. "ctrl+shift+1", empty == none
        juce::Colour colour { 0 };      // 0 alpha == "None" in the Python (color=None)
        bool hasColour = false;
        std::vector<std::string> tags;
        float trimStart = 0.0f, trimEnd = 1.0f;
        PlayMode playMode = PlayMode::Overlap;

        float progress = 0.0f;
    };
}
