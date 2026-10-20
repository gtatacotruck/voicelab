#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "FxSection.h"
#include "SoundSlotCard.h"
#include "AppModCanvasComponent.h"
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
  #include <cxxabi.h>
  #include <cstdlib>
#endif

namespace vlab::ui
{
    class ModderView : public juce::Component
    {
    public:
        explicit ModderView (juce::Component& rootIn) : root (rootIn)
        {
            setInterceptsMouseClicks (true, false); // swallow all clicks itself, see class comment
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0x18000000));

            if (hovered != nullptr)
                paintHighlight (g, hovered, juce::Colour (0xff2ee66b), 2.0f);
            if (selected != nullptr && selected != hovered)
                paintHighlight (g, selected, juce::Colour (0xffe6543f), 2.5f);

            g.setColour (juce::Colour (0xffb8c4bd));
            g.setFont (juce::Font (12.0f));
            g.drawText ("MODDER VIEW, hover to inspect, click to pin, Esc to unpin, Ctrl+Shift+M to exit",
                        getLocalBounds().removeFromTop (24).reduced (10, 0),
                        juce::Justification::centredLeft);

            if (selected != nullptr)
                paintInfoPanel (g);
            else if (hovered != nullptr)
                paintHoverLabel (g);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            auto* hit = findComponentUnderIgnoring (root, e.getPosition(), this);
            if (hit != hovered)
            {
                hovered = hit;
                repaint();
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (selected != nullptr && infoPanelBounds.contains (e.getPosition()))
                return;

            selected = hovered;
            repaint();
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey && selected != nullptr)
            {
                selected = nullptr;
                repaint();
                return true;
            }
            return false; // Ctrl+Shift+M itself is handled up in MainComponent, to also work as the "exit" toggle
        }

    private:
        static juce::Component* findComponentUnderIgnoring (juce::Component& start, juce::Point<int> posInStart,
                                                              juce::Component* ignore)
        {
            for (int i = start.getNumChildComponents() - 1; i >= 0; --i)
            {
                auto* child = start.getChildComponent (i);
                if (child == nullptr || child == ignore || ! child->isVisible())
                    continue;
                auto b = child->getBounds();
                if (b.contains (posInStart))
                    return findComponentUnderIgnoring (*child, posInStart - b.getPosition(), ignore);
            }
            return &start;
        }

        static juce::String demangledClassName (const juce::Component& c)
        {
            const char* raw = typeid (c).name();
#if defined(__GNUC__) || defined(__clang__)
            int status = 0;
            char* demangled = abi::__cxa_demangle (raw, nullptr, nullptr, &status);
            juce::String result = (status == 0 && demangled != nullptr) ? juce::String (demangled) : juce::String (raw);
            std::free (demangled); // free() is safe to call on nullptr
            return result;
#else
            return juce::String (raw);
#endif
        }

        void paintHighlight (juce::Graphics& g, juce::Component* c, juce::Colour colour, float thickness) const
        {
            auto bounds = root.getLocalArea (c, c->getLocalBounds());
            g.setColour (colour);
            g.drawRect (bounds, thickness);
        }

        void paintHoverLabel (juce::Graphics& g) const
        {
            auto bounds = root.getLocalArea (hovered, hovered->getLocalBounds());
            auto name = demangledClassName (*hovered);

            juce::Font f (12.0f);
            const int labelW = juce::jmin (400, f.getStringWidth (name) + 16);
            const int labelH = 20;
            int lx = bounds.getX();
            int ly = bounds.getY() - labelH - 2;
            if (ly < 24) ly = bounds.getBottom() + 2; // flip below if there's no room above (e.g. top bar)

            juce::Rectangle<int> labelBounds (lx, ly, labelW, labelH);
            g.setColour (juce::Colour (0xee121212));
            g.fillRect (labelBounds);
            g.setColour (juce::Colour (0xff2ee66b));
            g.drawRect (labelBounds, 1.0f);
            g.setFont (f);
            g.setColour (juce::Colour (0xffe8f5ec));
            g.drawText (name, labelBounds.reduced (6, 0), juce::Justification::centredLeft, true);
        }

        void paintInfoPanel (juce::Graphics& g)
        {
            juce::StringArray lines;
            lines.add (demangledClassName (*selected));
            if (selected->getName().isNotEmpty())
                lines.add ("component name: \"" + selected->getName() + "\"");

            auto b = selected->getBounds();
            lines.add ("bounds: " + juce::String (b.getX()) + ", " + juce::String (b.getY())
                       + "   " + juce::String (b.getWidth()) + " x " + juce::String (b.getHeight()));
            lines.add ("children: " + juce::String (selected->getNumChildComponents())
                       + "   visible: " + juce::String (selected->isVisible() ? "yes" : "no")
                       + "   enabled: " + juce::String (selected->isEnabled() ? "yes" : "no"));

            if (auto* fx = dynamic_cast<FxSection*> (selected))
                lines.add ("FxSection title: \"" + fx->getTitle() + "\"");
            if (auto* slot = dynamic_cast<SoundSlotCard*> (selected))
                lines.add ("SoundSlotCard slot index: " + juce::String (slot->getSlotIndex()));
            if (auto* canvas = dynamic_cast<AppModCanvasComponent*> (selected))
            {
                lines.add ("App Mod canvas title: \"" + canvas->getInfo().title + "\"");
                lines.add ("App Mod canvas id: " + juce::String (canvas->getInfo().id)
                           + "   takes over main view: " + juce::String (canvas->getInfo().takeOverMainView ? "yes" : "no"));
            }

            lines.add ({});
            lines.add ("parent chain:");
            auto* p = selected->getParentComponent();
            int depth = 1;
            while (p != nullptr && depth <= 12)
            {
                lines.add (juce::String::repeatedString ("  ", depth) + demangledClassName (*p));
                if (p == &root)
                    break;
                p = p->getParentComponent();
                ++depth;
            }

            const int panelW = 380;
            const int lineH = 16;
            const int pad = 10;
            const int panelH = pad * 2 + lineH * lines.size();
            infoPanelBounds = { getWidth() - panelW - 12, 30, panelW, juce::jmin (panelH, getHeight() - 42) };

            g.setColour (juce::Colour (0xee121212));
            g.fillRoundedRectangle (infoPanelBounds.toFloat(), 6.0f);
            g.setColour (juce::Colour (0xffe6543f));
            g.drawRoundedRectangle (infoPanelBounds.toFloat(), 6.0f, 1.5f);

            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (infoPanelBounds);

            g.setFont (juce::Font (12.0f));
            int y = infoPanelBounds.getY() + pad;
            for (int i = 0; i < lines.size(); ++i)
            {
                const bool isHeading = (i == 0) || lines[i] == "parent chain:";
                g.setColour (isHeading ? juce::Colour (0xffe6543f) : juce::Colour (0xffe8f5ec));
                g.drawText (lines[i], infoPanelBounds.getX() + pad, y, panelW - pad * 2, lineH,
                            juce::Justification::centredLeft, true);
                y += lineH;
            }
        }

        juce::Component& root;
        juce::Component* hovered = nullptr;
        juce::Component* selected = nullptr;
        juce::Rectangle<int> infoPanelBounds;
    };
}
