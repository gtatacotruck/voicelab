#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdlib>

namespace vlab::ui
{
    class SmoothViewport : public juce::Viewport, private juce::Timer
    {
    public:
        SmoothViewport()
        {
            setScrollBarThickness (10);
            setScrollOnDragEnabled (true);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto* content = getViewedComponent();
            const int maxY = content != nullptr ? juce::jmax (0, content->getHeight() - getMaximumVisibleHeight()) : 0;
            if (maxY <= 0)
            {
                juce::Viewport::mouseWheelMove (e, wheel);
                return;
            }

            // Trackpad vs wheel detection via timing
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double gap = now - lastEventTimeMs;
            lastEventTimeMs = now;

            if (gap < 200.0)
                avgGapMs = avgGapMs * 0.7 + gap * 0.3; // still inside the same gesture, keep averaging
            else
                avgGapMs = 1000.0; // long pause since the last event: treat this one as a fresh, isolated click

            if (avgGapMs < 40.0)
            {
                if (isTimerRunning())
                    stopTimer();
                targetY = -1;
                juce::Viewport::mouseWheelMove (e, wheel);
                return;
            }

            if (targetY < 0)
                targetY = getViewPositionY();

            constexpr float kPixelsPerNotch = 100.0f;
            targetY = juce::jlimit (0, maxY, targetY - (int) (wheel.deltaY * kPixelsPerNotch));

            if (! isTimerRunning())
                startTimerHz (60);
        }

    private:
        int targetY = -1;
        double lastEventTimeMs = 0.0;
        double avgGapMs = 1000.0;

        void timerCallback() override
        {
            if (targetY < 0) { stopTimer(); return; }
            const int cur = getViewPositionY();
            int next = cur + (targetY - cur) / 2;
            if (std::abs (targetY - next) <= 1) next = targetY;
            setViewPosition (getViewPositionX(), next);
            if (next == targetY)
            {
                targetY = -1;
                stopTimer();
            }
        }
    };
}
