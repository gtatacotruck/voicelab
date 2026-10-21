#pragma once
#include <juce_core/juce_core.h>
#include <functional>

// Global hook so UI code can translate a literal English string without a TranslationManager pointer...
namespace vlab::ui
{
    inline std::function<juce::String (const juce::String&)>& translateHook()
    {
        static std::function<juce::String (const juce::String&)> hook;
        return hook;
    }

    inline juce::String tr (const juce::String& english)
    {
        auto& h = translateHook();
        return h ? h (english) : english;
    }
}
