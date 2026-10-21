#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "Tr.h"
#include <functional>
#include <vector>

namespace vlab::ui
{
    class FxSection : public juce::Component
    {
    public:
        // Shared by every section so the whole app flips between the roomy classic rows and the
        // denser one-line rows of the new UI in one go (see MainComponent::applyUiMode()).
        static inline bool compactMode = false;


        static inline bool sliderWheelEnabled = false;

        FxSection (juce::String titleIn, juce::String glyphText)
            : title (std::move (titleIn)), glyph (std::move (glyphText)) {}

        // Get section title for display
        const juce::String& getTitle() const { return title; }

        juce::Slider& addSlider (const juce::String& labelText, double lo, double hi, double def,
                                  std::function<void (double)> onChange, int decimalPlaces = 2)
        {
            auto row = std::make_unique<Row>();
            row->baseText = labelText;
            row->decimals = decimalPlaces;
            row->label.setText (formatLabel (tr (labelText), def, decimalPlaces), juce::dontSendNotification);
            row->label.setFont (juce::Font (12.5f));
            row->label.setColour (juce::Label::textColourId, palette().text);
            addAndMakeVisible (row->label);

            row->slider.setScrollWheelEnabled (sliderWheelEnabled);
            row->slider.setRange (lo, hi);
            row->slider.setValue (def, juce::dontSendNotification);
            row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            addAndMakeVisible (row->slider);

            auto* labelPtr = &row->label;
            row->slider.onValueChange = [labelPtr, labelText, decimalPlaces, onChange, sliderPtr = &row->slider]
            {
                labelPtr->setText (formatLabel (tr (labelText), sliderPtr->getValue(), decimalPlaces), juce::dontSendNotification);
                onChange (sliderPtr->getValue());
            };

            auto& ref = row->slider;
            rows.push_back (std::move (row));
            resized();
            return ref;
        }

        juce::ComboBox& addCombo (const juce::String& labelText, const juce::StringArray& options,
                                   int defaultIndex, std::function<void (int)> onChange)
        {
            auto row = std::make_unique<Row>();
            row->isCombo = true;
            row->baseText = labelText;
            row->comboOptions = options;
            row->label.setText (tr (labelText), juce::dontSendNotification);
            row->label.setFont (juce::Font (12.5f));
            row->label.setColour (juce::Label::textColourId, palette().text);
            addAndMakeVisible (row->label);

            row->combo.setScrollWheelEnabled (sliderWheelEnabled);
            for (int i = 0; i < options.size(); ++i)
                row->combo.addItem (tr (options[i]), i + 1);
            row->combo.setSelectedItemIndex (defaultIndex, juce::dontSendNotification);
            row->combo.onChange = [onChange, comboPtr = &row->combo] { onChange (comboPtr->getSelectedItemIndex()); };
            addAndMakeVisible (row->combo);

            auto& ref = row->combo;
            rows.push_back (std::move (row));
            resized();
            return ref;
        }

        juce::ToggleButton& addToggle (const juce::String& labelText, bool def, std::function<void (bool)> onChange)
        {
            auto row = std::make_unique<Row>();
            row->isToggle = true;
            row->baseText = labelText;
            row->toggle.setButtonText (tr (labelText));
            row->toggle.setToggleState (def, juce::dontSendNotification);
            row->toggle.onClick = [onChange, tp = &row->toggle] { onChange (tp->getToggleState()); };
            addAndMakeVisible (row->toggle);
            auto& ref = row->toggle;
            rows.push_back (std::move (row));
            resized();
            return ref;
        }

        juce::TextButton& addButton (const juce::String& labelText, const juce::String& tooltip,
                                      std::function<void()> onClick)
        {
            auto row = std::make_unique<Row>();
            row->isButton = true;
            row->baseText = labelText;
            row->button.setButtonText (tr (labelText));
            if (tooltip.isNotEmpty()) row->button.setTooltip (tooltip);
            row->button.onClick = onClick;
            addAndMakeVisible (row->button);
            auto& ref = row->button;
            rows.push_back (std::move (row));
            resized();
            return ref;
        }

        void addCustomComponent (juce::Component& comp, int height)
        {
            auto row = std::make_unique<Row>();
            row->isCustom = true;
            row->customComp = &comp;
            row->customHeight = juce::jmax (24, height);
            addAndMakeVisible (comp);
            rows.push_back (std::move (row));
            resized();
        }


        void retranslate()
        {
            for (auto& row : rows)
            {
                if (row->isCustom)
                    continue;
                if (row->isToggle)       row->toggle.setButtonText (tr (row->baseText));
                else if (row->isButton)  row->button.setButtonText (tr (row->baseText));
                else if (row->isCombo)
                {
                    row->label.setText (tr (row->baseText), juce::dontSendNotification);
                    for (int i = 0; i < row->comboOptions.size(); ++i)
                        row->combo.changeItemText (i + 1, tr (row->comboOptions[i]));
                    row->combo.repaint();
                }
                else
                    row->label.setText (formatLabel (tr (row->baseText), row->slider.getValue(), row->decimals),
                                         juce::dontSendNotification);
            }
            repaint();
        }

        // Every English string this section can show, used by Settings > "Export Language Template".
        void collectTranslatableText (juce::StringArray& out) const
        {
            out.addIfNotAlreadyThere (title);
            for (auto& row : rows)
            {
                if (row->isCustom)
                    continue;
                out.addIfNotAlreadyThere (row->baseText);
                for (auto& o : row->comboOptions)
                    out.addIfNotAlreadyThere (o);
            }
        }

        // Custom components can carry their own labels/buttons, so the app-wide translation pass needs to reach them.
        void forEachCustomComponent (const std::function<void (juce::Component&)>& fn)
        {
            for (auto& row : rows)
                if (row->isCustom && row->customComp != nullptr)
                    fn (*row->customComp);
        }

        void setCollapsible (bool shouldBeCollapsible) { collapsible = shouldBeCollapsible; }
        bool isCollapsible() const { return collapsible; }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (collapsible && e.mouseWasClicked() && e.y < getHeaderHeight())
                setCollapsed (! collapsed);
        }

        bool isCollapsed() const { return collapsed; }

        void setCollapsed (bool shouldBeCollapsed)
        {
            if (collapsed == shouldBeCollapsed)
                return;
            collapsed = shouldBeCollapsed;
            for (auto& row : rows)
            {
                juce::Component* comps[] = { &row->label, &row->slider, &row->toggle, &row->combo, &row->button };
                for (auto* c : comps)
                    c->setVisible (! collapsed);
                if (row->isCustom && row->customComp)
                    row->customComp->setVisible (! collapsed);
            }
            resized();
            if (onCollapsedChanged) onCollapsedChanged();
        }

        std::function<void()> onCollapsedChanged;

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat();
            drawPanelBackground (g, bounds, p, u, u.cornerRadius);

            auto header = bounds.removeFromTop ((float) getHeaderHeight()).reduced (14.0f, 8.0f);
            auto iconBox = header.removeFromLeft (20.0f);
            g.setColour (p.accentDim.withAlpha (0.4f));
            g.fillRoundedRectangle (iconBox, juce::jmin (5.0f, u.cornerRadius));
            g.setColour (p.accent);
            g.setFont (juce::Font (12.0f * u.fontScale, juce::Font::bold));
            g.drawText (glyph, iconBox.toNearestInt(), juce::Justification::centred);

            auto chevronBox = header.removeFromRight (collapsible ? 20.0f : 0.0f);
            if (collapsible)
            {
                g.setColour (p.accent);
                g.setFont (juce::Font (14.0f, juce::Font::bold));
                g.drawText (collapsed ? "+" : "-", chevronBox.toNearestInt(), juce::Justification::centred);
            }

            header.removeFromLeft (8.0f);
            g.setColour (p.accent);
            g.setFont (juce::Font (13.0f * u.fontScale, juce::Font::bold));
            g.drawFittedText (tr (title).toUpperCase(), header.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        void resized() override
        {
            auto a = getLocalBounds();
            a.removeFromTop (getHeaderHeight());
            if (collapsed)
                return;
            a.reduce (16, 0);
            const bool inline_ = compactMode;
            const int gap = getRowGap();
            for (auto& row : rows)
            {
                auto r = a.removeFromTop (getRowHeight (*row));
                if (row->isToggle)
                {
                    row->toggle.setBounds (r);
                }
                else if (row->isCombo)
                {
                    if (inline_)
                    {
                        row->label.setBounds (r.removeFromLeft (labelWidthFor (r.getWidth())));
                        row->combo.setBounds (r.reduced (0, 2));
                    }
                    else
                    {
                        row->label.setBounds (r.removeFromTop (16));
                        row->combo.setBounds (r.removeFromTop (20));
                    }
                }
                else if (row->isButton)
                {
                    row->button.setBounds (r);
                }
                else if (row->isCustom)
                {
                    if (row->customComp) row->customComp->setBounds (r);
                }
                else
                {
                    if (inline_)
                    {
                        row->label.setBounds (r.removeFromLeft (labelWidthFor (r.getWidth())));
                        row->slider.setBounds (r);
                    }
                    else
                    {
                        row->label.setBounds (r.removeFromTop (16));
                        row->slider.setBounds (r.removeFromTop (18));
                    }
                }
                a.removeFromTop (gap);
            }
        }

        int getPreferredHeight() const
        {
            if (collapsed)
                return getHeaderHeight() + 8;
            int total = getHeaderHeight() + (compactMode ? 8 : 12);
            for (auto& row : rows)
                total += getRowHeight (*row) + getRowGap();
            return total;
        }

    private:
        juce::String title, glyph;
        static constexpr int rowHeight = 34, rowGap = 8, headerHeight = 40;
        static constexpr int compactRowGap = 4, compactHeaderHeight = 32;
        bool collapsed = false;
        bool collapsible = false;

        struct Row
        {
            bool isToggle = false;
            bool isCombo = false;
            bool isButton = false;
            bool isCustom = false;
            juce::Label label;
            juce::Slider slider;
            juce::ToggleButton toggle;
            juce::ComboBox combo;
            juce::TextButton button;
            juce::Component* customComp = nullptr; 
            int customHeight = 0;
            juce::String baseText;         
            int decimals = 2;              
            juce::StringArray comboOptions;   
        };
        std::vector<std::unique_ptr<Row>> rows;

        static int getHeaderHeight() { return compactMode ? compactHeaderHeight : headerHeight; }
        static int getRowGap()       { return compactMode ? compactRowGap : rowGap; }

        static int getRowHeight (const Row& row)
        {
            if (row.isCustom)
                return row.customHeight;
            if (! compactMode)
                return rowHeight;
            if (row.isToggle) return 24;
            if (row.isButton) return 28;
            if (row.isCombo)  return 28;
            return 26; // slider: label and slider share one line
        }

        static int labelWidthFor (int rowWidth)
        {
            return juce::jlimit (96, 170, (int) (rowWidth * 0.46f));
        }

        static juce::String formatLabel (const juce::String& base, double value, int decimals)
        {
            return base + ": " + juce::String (value, decimals);
        }
    };
}
