#pragma once
#include <juce_core/juce_core.h>
#include <functional>
//dont neglect me papa
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
