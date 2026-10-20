#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Mods/AppModHost.h"
#include "LookAndFeel.h"

namespace vlab::ui
{
    class AppModCanvasComponent : public juce::Component
    {
    public:
        AppModCanvasComponent (vlab::AppModHost& hostIn, vlab::AppModCanvasInfo& infoIn)
            : host (hostIn), info (infoIn)
        {
            setWantsKeyboardFocus (true);
            setInterceptsMouseClicks (true, true);
        }

        const vlab::AppModCanvasInfo& getInfo() const { return info; }

        void refreshFrame()
        {
            if (getWidth() <= 0 || getHeight() <= 0)
                return;
            if (host.paintCanvas (info, getWidth(), getHeight(), frame))
                repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            if (frame.isValid())
            {
                g.drawImageAt (frame, 0, 0);
            }
            else
            {
                g.fillAll (p.panelAlt);
                g.setColour (p.textDim);
                g.setFont (juce::Font (12.0f));
                g.drawText ("(" + info.title + ", waiting for first frame)", getLocalBounds(), juce::Justification::centred);
            }
            g.setColour (p.stroke);
            g.drawRect (getLocalBounds(), 1);
        }

        void resized() override { frame = {}; } // force a resize+repaint on next refreshFrame()

        void mouseMove (const juce::MouseEvent& e) override      { send (0, e); }
        void mouseDown (const juce::MouseEvent& e) override      { grabKeyboardFocus(); send (1, e); }
        void mouseUp   (const juce::MouseEvent& e) override      { send (2, e); }
        void mouseDrag (const juce::MouseEvent& e) override      { send (3, e); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            host.sendMouse (info, 4, 0, (int) (w.deltaY * 100.0f), buttonsMask (e));
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            host.sendKey (info, key.getKeyCode(), true, key.getTextDescription().toLowerCase());
            return true; // consumed, a full-view canvas owns all keyboard input while its tab is showing
        }

    private:
        vlab::AppModHost& host;
        vlab::AppModCanvasInfo& info;
        juce::Image frame;

        static int buttonsMask (const juce::MouseEvent& e)
        {
            int mask = 0;
            if (e.mods.isLeftButtonDown())   mask |= 1;
            if (e.mods.isRightButtonDown())  mask |= 2;
            if (e.mods.isMiddleButtonDown()) mask |= 4;
            return mask;
        }

        void send (int eventType, const juce::MouseEvent& e)
        {
            host.sendMouse (info, eventType, e.x, e.y, buttonsMask (e));
        }
    };
}
