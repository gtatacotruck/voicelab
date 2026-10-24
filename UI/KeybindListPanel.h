#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include <functional>
#include <vector>

namespace vlab::ui
{
    // every keybind in the app, click Clear on a row to drop it
    class KeybindListPanel : public juce::Component, private juce::ListBoxModel
    {
    public:
        struct Entry
        {
            juce::String owner, combo;
            std::function<void()> clear; // empty for built in shortcuts
        };

        std::function<std::vector<Entry>()> provider;
        std::function<void()> onClearAll;

        KeybindListPanel()
        {
            addAndMakeVisible (summary);
            summary.setFont (juce::Font (12.0f));
            addAndMakeVisible (clearAllButton);
            clearAllButton.getProperties().set ("danger", true);
            clearAllButton.onClick = [this] { if (onClearAll) onClearAll(); };
            addAndMakeVisible (list);
            list.setModel (this);
            list.setRowHeight (26);
            list.setOutlineThickness (0);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        }

        void refresh()
        {
            entries = provider ? provider() : std::vector<Entry>();
            int removable = 0;
            for (auto& e : entries) if (e.clear) ++removable;
            summary.setText (entries.empty() ? "No keybinds set." : juce::String ((int) entries.size()) + " keybind" + (entries.size() == 1 ? "" : "s"),
                             juce::dontSendNotification);
            summary.setColour (juce::Label::textColourId, palette().textDim);
            clearAllButton.setEnabled (removable > 0);
            list.updateContent();
            list.repaint();
        }

        void resized() override
        {
            auto a = getLocalBounds();
            auto top = a.removeFromTop (28);
            clearAllButton.setBounds (top.removeFromRight (150));
            summary.setBounds (top);
            a.removeFromTop (4);
            list.setBounds (a);
        }

    private:
        juce::Label summary;
        juce::TextButton clearAllButton { "Clear All Keybinds" };
        juce::ListBox list;
        std::vector<Entry> entries;

        int getNumRows() override { return (int) entries.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            if (row < 0 || row >= (int) entries.size())
                return;
            const auto& e = entries[(size_t) row];
            const auto& p = palette();
            if (row % 2 == 0)
                g.fillAll (p.panelAlt.withAlpha (0.35f));

            g.setFont (juce::Font (12.5f));
            g.setColour (p.text);
            g.drawText (e.owner, 8, 0, w - 220, h, juce::Justification::centredLeft, true);

            auto chip = juce::Rectangle<float> ((float) w - 200.0f, 3.0f, 120.0f, (float) h - 6.0f);
            g.setColour (p.panel.brighter (0.1f));
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (p.accent);
            g.setFont (juce::Font (12.0f, juce::Font::bold));
            g.drawText (e.combo.toUpperCase(), chip.toNearestInt(), juce::Justification::centred, true);

            if (e.clear)
            {
                g.setColour (p.danger);
                g.setFont (juce::Font (12.0f));
                g.drawText ("Clear", w - 72, 0, 64, h, juce::Justification::centred);
            }
            else
            {
                g.setColour (p.textDim);
                g.setFont (juce::Font (11.0f));
                g.drawText ("built in", w - 72, 0, 64, h, juce::Justification::centred);
            }
        }

        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (row < 0 || row >= (int) entries.size() || ! entries[(size_t) row].clear)
                return;
            if (e.x > list.getWidth() - 80)
            {
                auto fn = entries[(size_t) row].clear;
                fn();
                refresh();
            }
        }
    };
}
