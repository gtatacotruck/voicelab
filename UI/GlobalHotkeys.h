#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace vlab::ui
{
    class GlobalHotkeyWatcher : private juce::Timer
    {
    public:
        std::function<void (const juce::String&)> onTriggered;

        void setWatchList (const juce::StringArray& descriptions)
        {
            entries.clear();
            for (auto& d : descriptions)
            {
                if (d.isEmpty())
                    continue;
                entries.push_back ({ d, juce::KeyPress::createFromDescription (d), false });
            }

           #if JUCE_WINDOWS
            if (! entries.empty() && ! isTimerRunning())
                startTimer (16);
            else if (entries.empty() && isTimerRunning())
                stopTimer();
           #endif
        }

    private:
        struct Entry
        {
            juce::String description;
            juce::KeyPress key;
            bool wasDown;
        };
        std::vector<Entry> entries;

       #if JUCE_WINDOWS
        void timerCallback() override
        {
            if (juce::Process::isForegroundProcess())
                return;

            for (auto& e : entries)
            {
                const bool down = isComboDown (e.key);
                if (down && ! e.wasDown && onTriggered)
                    onTriggered (e.description);
                e.wasDown = down;
            }
        }

        static bool isComboDown (const juce::KeyPress& kp)
        {
            const int vk = toVirtualKey (kp.getKeyCode());
            if (vk == 0 || (GetAsyncKeyState (vk) & 0x8000) == 0)
                return false;

            const auto mods = kp.getModifiers();
            if (mods.isCtrlDown()  != isDown (VK_CONTROL)) return false;
            if (mods.isShiftDown() != isDown (VK_SHIFT))   return false;
            if (mods.isAltDown()   != isDown (VK_MENU))    return false;
            return true;
        }

        static bool isDown (int vk) { return (GetAsyncKeyState (vk) & 0x8000) != 0; }

        static int toVirtualKey (int juceKeyCode)
        {
            if ((juceKeyCode >= '0' && juceKeyCode <= '9') || (juceKeyCode >= 'A' && juceKeyCode <= 'Z'))
                return juceKeyCode;
            if (juceKeyCode >= juce::KeyPress::F1Key && juceKeyCode <= juce::KeyPress::F24Key)
                return VK_F1 + (juceKeyCode - juce::KeyPress::F1Key);

            if (juceKeyCode == juce::KeyPress::spaceKey)     return VK_SPACE;
            if (juceKeyCode == juce::KeyPress::escapeKey)    return VK_ESCAPE;
            if (juceKeyCode == juce::KeyPress::returnKey)    return VK_RETURN;
            if (juceKeyCode == juce::KeyPress::tabKey)       return VK_TAB;
            if (juceKeyCode == juce::KeyPress::deleteKey)    return VK_DELETE;
            if (juceKeyCode == juce::KeyPress::backspaceKey) return VK_BACK;
            if (juceKeyCode == juce::KeyPress::upKey)        return VK_UP;
            if (juceKeyCode == juce::KeyPress::downKey)      return VK_DOWN;
            if (juceKeyCode == juce::KeyPress::leftKey)      return VK_LEFT;
            if (juceKeyCode == juce::KeyPress::rightKey)     return VK_RIGHT;
            if (juceKeyCode == juce::KeyPress::homeKey)      return VK_HOME;
            if (juceKeyCode == juce::KeyPress::endKey)       return VK_END;
            if (juceKeyCode == juce::KeyPress::pageUpKey)    return VK_PRIOR;
            if (juceKeyCode == juce::KeyPress::pageDownKey)  return VK_NEXT;
            if (juceKeyCode == juce::KeyPress::insertKey)    return VK_INSERT;
            return 0;
        }
       #else
        void timerCallback() override {}
       #endif
    };
}
