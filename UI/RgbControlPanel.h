#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "RgbEngine.h"
#include <vector>

namespace vlab::ui
{
    class RgbControlPanel : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
    {
    public:
        std::function<void()> onSettingsChanged; // fire after any change, so the caller can persist to disk

        explicit RgbControlPanel (RgbEngine& engineIn) : engine (engineIn)
        {
            working = engine.getSettings();

            addAndMakeVisible (titleLabel);
            titleLabel.setText ("RGB / Colour Cycling", juce::dontSendNotification);
            titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, palette().accent);

            addAndMakeVisible (subtitleLabel);
            subtitleLabel.setText (
                "Animates the accent colour (buttons, sliders, highlights) on top of whatever theme is active. "
                "Pick a mode, dial in a speed, and for the gradient-based modes set your own colour stops below. "
                "Off leaves the theme exactly as Theme Creator left it.",
                juce::dontSendNotification);
            subtitleLabel.setFont (juce::Font (12.0f));
            subtitleLabel.setColour (juce::Label::textColourId, palette().textDim);
            subtitleLabel.setJustificationType (juce::Justification::topLeft);

            addAndMakeVisible (modeLabel);
            modeLabel.setText ("Mode:", juce::dontSendNotification);
            modeLabel.setFont (juce::Font (12.5f));
            modeLabel.setColour (juce::Label::textColourId, palette().text);

            addAndMakeVisible (modeCombo);
            auto names = rgbModeNames();
            for (int i = 0; i < names.size(); ++i)
                modeCombo.addItem (names[i], i + 1);
            modeCombo.setSelectedItemIndex ((int) working.mode, juce::dontSendNotification);
            modeCombo.onChange = [this]
            {
                working.mode = (RgbMode) modeCombo.getSelectedItemIndex();
                updateGradientRowsEnabled();
                push();
            };

            addAndMakeVisible (previewSwatch);

            addAdvancedSlider (speedLabel, speedSlider, "Speed (cycles/sec)", 0.02, 5.0, working.speedHz,
                [this] (double v) { working.speedHz = (float) v; push(); });
            addAdvancedSlider (saturationLabel, saturationSlider, "Saturation (Rainbow modes)", 0.0, 1.0, working.saturation,
                [this] (double v) { working.saturation = (float) v; push(); });
            addAdvancedSlider (brightnessLabel, brightnessSlider, "Brightness", 0.1, 1.5, working.brightness,
                [this] (double v) { working.brightness = (float) v; push(); });
            addAdvancedSlider (strobeDutyLabel, strobeDutySlider, "Strobe On-Time", 0.05, 0.95, working.strobeDuty,
                [this] (double v) { working.strobeDuty = (float) v; push(); });

            addAndMakeVisible (applyToBackgroundToggle);
            applyToBackgroundToggle.setButtonText ("Also tint background / panels (subtle wash)");
            applyToBackgroundToggle.setToggleState (working.applyToBackground, juce::dontSendNotification);
            applyToBackgroundToggle.onClick = [this]
            {
                working.applyToBackground = applyToBackgroundToggle.getToggleState();
                push();
            };

            addAndMakeVisible (gradientLabel);
            gradientLabel.setText ("Custom Gradient Stops (Gradient Cycle & Chase):", juce::dontSendNotification);
            gradientLabel.setFont (juce::Font (13.0f, juce::Font::bold));
            gradientLabel.setColour (juce::Label::textColourId, palette().accent);

            addAndMakeVisible (addStopButton);
            addStopButton.setButtonText ("+ Add Stop");
            addStopButton.onClick = [this]
            {
                if (working.gradientStops.size() >= maxStops) return;
                auto last = working.gradientStops.empty() ? juce::Colours::hotpink : working.gradientStops.back();
                working.gradientStops.push_back (last.withRotatedHue (0.15f));
                rebuildStopRows();
                push();
            };

            addAndMakeVisible (removeStopButton);
            removeStopButton.setButtonText ("- Remove Stop");
            removeStopButton.getProperties().set ("danger", true);
            removeStopButton.onClick = [this]
            {
                if (working.gradientStops.size() <= 2) return;
                working.gradientStops.pop_back();
                rebuildStopRows();
                push();
            };

            addAndMakeVisible (statusLabel);
            statusLabel.setFont (juce::Font (11.5f));
            statusLabel.setColour (juce::Label::textColourId, palette().textDim);
            statusLabel.setText ("Off = the static theme colour, exactly as picked in Theme Creator.", juce::dontSendNotification);

            rebuildStopRows();
            updateGradientRowsEnabled();
            startTimerHz (30); // drives the live preview swatch only; RgbEngine has its own timer for the real theme colours
        }

        ~RgbControlPanel() override { stopTimer(); }

        void resized() override
        {
            auto a = getLocalBounds().reduced (4);
            titleLabel.setBounds (a.removeFromTop (28));
            subtitleLabel.setBounds (a.removeFromTop (56));
            a.removeFromTop (10);

            auto modeRow = a.removeFromTop (28);
            modeLabel.setBounds (modeRow.removeFromLeft (60));
            previewSwatch.setBounds (modeRow.removeFromRight (36).reduced (2));
            modeRow.removeFromRight (6);
            modeCombo.setBounds (modeRow);
            a.removeFromTop (10);

            advRow (a, speedLabel, speedSlider);
            advRow (a, saturationLabel, saturationSlider);
            advRow (a, brightnessLabel, brightnessSlider);
            advRow (a, strobeDutyLabel, strobeDutySlider);

            applyToBackgroundToggle.setBounds (a.removeFromTop (28));
            a.removeFromTop (10);

            gradientLabel.setBounds (a.removeFromTop (20));
            a.removeFromTop (6);

            for (auto& row : stopRows)
            {
                auto r = a.removeFromTop (36);
                row->swatch.setBounds (r.removeFromLeft (48).reduced (2));
                r.removeFromLeft (8);
                row->hexEditor.setBounds (r.removeFromLeft (juce::jmin (140, r.getWidth())));
                a.removeFromTop (6);
            }

            auto btnRow = a.removeFromTop (30);
            addStopButton.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2 - 4));
            btnRow.removeFromLeft (8);
            removeStopButton.setBounds (btnRow);
            a.removeFromTop (10);

            statusLabel.setBounds (a.removeFromTop (18));
        }

        int getPreferredHeight() const
        {
            return 28 + 56 + 10 + 28 + 10
                 + 4 * 36            // speed / saturation / brightness / strobe duty
                 + 28 + 10           // background toggle
                 + 20 + 6            // gradient label
                 + (int) stopRows.size() * 42
                 + 30 + 10 + 18 + 12;
        }

    private:
        RgbEngine& engine;
        RgbSettings working;
        static constexpr size_t maxStops = 8;

        juce::Label titleLabel, subtitleLabel, modeLabel, gradientLabel, statusLabel;
        juce::ComboBox modeCombo;
        juce::Label speedLabel, saturationLabel, brightnessLabel, strobeDutyLabel;
        juce::Slider speedSlider, saturationSlider, brightnessSlider, strobeDutySlider;
        juce::ToggleButton applyToBackgroundToggle;
        juce::TextButton addStopButton, removeStopButton;

        // Small live-updating box showing the currently animated accent colour.
        struct PreviewSwatch : public juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.setColour (palette().accent);
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
                g.setColour (palette().stroke);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
            }
        };
        PreviewSwatch previewSwatch;

        struct StopRow
        {
            juce::TextButton swatch;
            juce::TextEditor hexEditor;
        };
        std::vector<std::unique_ptr<StopRow>> stopRows;
        StopRow* activeEditRow = nullptr;

        void addAdvancedSlider (juce::Label& label, juce::Slider& slider, const juce::String& text,
                                 double lo, double hi, double def, std::function<void (double)> onChange)
        {
            addAndMakeVisible (label);
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (12.5f));
            label.setColour (juce::Label::textColourId, palette().text);

            addAndMakeVisible (slider);
            slider.setRange (lo, hi);
            slider.setValue (def, juce::dontSendNotification);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            slider.onValueChange = [&slider, onChange] { onChange (slider.getValue()); };
        }

        static void advRow (juce::Rectangle<int>& a, juce::Label& label, juce::Slider& slider)
        {
            auto r = a.removeFromTop (30);
            label.setBounds (r.removeFromLeft (190));
            slider.setBounds (r);
            a.removeFromTop (6);
        }

        void updateGradientRowsEnabled()
        {
            const bool relevant = (working.mode == RgbMode::GradientCycle || working.mode == RgbMode::Chase);
            const float alpha = relevant ? 1.0f : 0.4f;
            for (auto& row : stopRows)
            {
                row->swatch.setAlpha (alpha);
                row->hexEditor.setAlpha (alpha);
            }
            addStopButton.setAlpha (alpha);
            removeStopButton.setAlpha (alpha);
            saturationSlider.setAlpha (working.mode == RgbMode::RainbowCycle || working.mode == RgbMode::StrobeRainbow ? 1.0f : 0.4f);
            strobeDutySlider.setAlpha (working.mode == RgbMode::Strobe || working.mode == RgbMode::StrobeRainbow ? 1.0f : 0.4f);
        }

        void rebuildStopRows()
        {
            stopRows.clear();
            for (size_t i = 0; i < working.gradientStops.size(); ++i)
            {
                auto row = std::make_unique<StopRow>();
                addAndMakeVisible (row->swatch);
                row->swatch.setColour (juce::TextButton::buttonColourId, working.gradientStops[i]);
                auto* rowPtr = row.get();
                const size_t idx = i;
                row->swatch.onClick = [this, rowPtr, idx] { openStopPicker (*rowPtr, idx); };

                addAndMakeVisible (row->hexEditor);
                row->hexEditor.setFont (juce::Font (12.0f));
                row->hexEditor.setText ("#" + working.gradientStops[idx].toDisplayString (false).toLowerCase(),
                                         juce::dontSendNotification);
                row->hexEditor.onReturnKey = [this, rowPtr, idx] { applyStopHex (*rowPtr, idx); };
                row->hexEditor.onFocusLost = [this, rowPtr, idx] { applyStopHex (*rowPtr, idx); };

                stopRows.push_back (std::move (row));
            }
            resized();
            if (auto* p = getParentComponent())
                p->resized();
        }

        void applyStopHex (StopRow& row, size_t idx)
        {
            if (idx >= working.gradientStops.size()) return;
            auto hex = row.hexEditor.getText().trim();
            if (hex.startsWithChar ('#')) hex = hex.substring (1);
            if (hex.length() == 6) hex = "ff" + hex;
            if (hex.length() != 8 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
            {
                statusLabel.setText ("Invalid hex colour, use #RRGGBB", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                row.hexEditor.setText ("#" + working.gradientStops[idx].toDisplayString (false).toLowerCase(),
                                        juce::dontSendNotification);
                return;
            }
            const auto c = juce::Colour::fromString (hex);
            working.gradientStops[idx] = c;
            row.swatch.setColour (juce::TextButton::buttonColourId, c);
            statusLabel.setText ({}, juce::dontSendNotification);
            push();
        }

        void openStopPicker (StopRow& row, size_t idx)
        {
            activeEditRow = &row;
            activeEditIndex = idx;
            auto* selector = new juce::ColourSelector (
                juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders);
            selector->setSize (300, 360);
            selector->setCurrentColour (idx < working.gradientStops.size() ? working.gradientStops[idx] : juce::Colours::white);
            selector->addChangeListener (this);
            juce::CallOutBox::launchAsynchronously (std::unique_ptr<Component> (selector),
                row.swatch.getScreenBounds(), nullptr);
        }

        size_t activeEditIndex = 0;

        void changeListenerCallback (juce::ChangeBroadcaster* src) override
        {
            if (activeEditRow == nullptr || activeEditIndex >= working.gradientStops.size()) return;
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
            {
                const auto c = cs->getCurrentColour();
                working.gradientStops[activeEditIndex] = c;
                activeEditRow->swatch.setColour (juce::TextButton::buttonColourId, c);
                activeEditRow->hexEditor.setText ("#" + c.toDisplayString (false).toLowerCase(), juce::dontSendNotification);
                push();
            }
        }

        void timerCallback() override { previewSwatch.repaint(); }

        void push()
        {
            engine.setSettings (working);
            updateGradientRowsEnabled();
            if (onSettingsChanged) onSettingsChanged();
        }
    };
}
