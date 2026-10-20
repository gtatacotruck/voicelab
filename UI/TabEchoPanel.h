#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "SystemAudioCapture.h"
#include "../AudioEngineParams.h"

namespace vlab::ui
{
    class TabEchoPanel : public juce::Component
    {
    public:
        TabEchoPanel (vlab::AudioEngineParams& paramsIn, vlab::SystemAudioCapture& captureIn)
            : params (paramsIn), capture (captureIn)
        {
            addAndMakeVisible (titleLabel);
            titleLabel.setText ("Tab Echo", juce::dontSendNotification);
            titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, palette().text);

            addAndMakeVisible (infoButton);
            infoButton.setButtonText ("?");
            infoButton.onClick = [this] { showInfo(); };

            addAndMakeVisible (enabledToggle);
            enabledToggle.setButtonText ("Enabled");
            enabledToggle.onClick = [this] { applyDevice(); };

            addAndMakeVisible (deviceCombo);
            refreshDevices();
            deviceCombo.onChange = [this] { applyDevice(); };

            addAndMakeVisible (refreshButton);
            refreshButton.setButtonText ("Refresh");
            refreshButton.onClick = [this] { refreshDevices(); };

            addAndMakeVisible (volLabel);
            volLabel.setText ("Vol", juce::dontSendNotification);
            volLabel.setFont (juce::Font (12.0f));
            volLabel.setColour (juce::Label::textColourId, palette().textDim);

            addAndMakeVisible (volumeSlider);
            volumeSlider.setRange (0.0, 1.5);
            volumeSlider.setValue (params.browserAudioVol.load(), juce::dontSendNotification);
            volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
            volumeSlider.onValueChange = [this] { params.browserAudioVol = (float) volumeSlider.getValue(); };

            addAndMakeVisible (statusLabel);
            statusLabel.setFont (juce::Font (11.0f));
            statusLabel.setColour (juce::Label::textColourId, palette().textDim);
            refreshStatus();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (p.panelAlt);
            g.fillRoundedRectangle (b, u.cornerRadius * 0.7f);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (b.reduced (0.5f), u.cornerRadius * 0.7f, u.borderWidth);
        }

        void syncFromState()
        {
            enabledToggle.setToggleState (capture.isActive(), juce::dontSendNotification);
            volumeSlider.setValue (params.browserAudioVol.load(), juce::dontSendNotification);
            refreshStatus();
        }

        void resized() override
        {
            auto full = getLocalBounds().reduced (8, 4);
            auto statusRow = full.removeFromBottom (16);
            statusLabel.setBounds (statusRow);
            full.removeFromBottom (2);

            auto a = full;

            auto left = a.removeFromLeft (juce::jmin (150, a.getWidth() / 5));
            titleLabel.setBounds (left.removeFromLeft (left.getWidth() - 22));
            infoButton.setBounds (left);
            a.removeFromLeft (10);

            enabledToggle.setBounds (a.removeFromLeft (90));
            a.removeFromLeft (8);

            refreshButton.setBounds (a.removeFromRight (70));
            a.removeFromRight (8);

            volLabel.setBounds (a.removeFromLeft (26));
            volumeSlider.setBounds (a.removeFromLeft (juce::jmin (170, a.getWidth() / 2)));
            a.removeFromLeft (8);

            deviceCombo.setBounds (a); // whatever's left on the row
        }

    private:
        vlab::AudioEngineParams& params;
        vlab::SystemAudioCapture& capture;

        juce::Label titleLabel;
        juce::TextButton infoButton;
        juce::ToggleButton enabledToggle;
        juce::ComboBox deviceCombo;
        juce::TextButton refreshButton;
        juce::Label volLabel;
        juce::Slider volumeSlider;
        juce::Label statusLabel;

        void refreshDevices()
        {
            const auto previouslySelected = deviceCombo.getText();
            deviceCombo.clear();
            auto names = capture.getAvailableInputNames();
            for (int i = 0; i < names.size(); ++i)
                deviceCombo.addItem (names[i], i + 1);

            if (names.isEmpty())
                return;

            // Prefer loopback/monitor source, fallback to first device
            int pick = names.indexOf (previouslySelected);
            if (pick < 0)
            {
                for (int i = 0; i < names.size(); ++i)
                {
                    const auto lower = names[i].toLowerCase();
                    if (lower.contains ("monitor") || lower.contains ("loopback")
                        || lower.contains ("stereo mix") || lower.contains ("blackhole"))
                    {
                        pick = i;
                        break;
                    }
                }
            }
            if (pick < 0)
                pick = 0;

            deviceCombo.setSelectedItemIndex (pick, juce::dontSendNotification);

            if (enabledToggle.getToggleState())
                applyDevice();
            else
                refreshStatus();
        }

        void applyDevice()
        {
            if (! enabledToggle.getToggleState())
            {
                capture.close();
                refreshStatus();
                return;
            }
            if (deviceCombo.getSelectedId() == 0 || deviceCombo.getText().isEmpty())
            {
                statusLabel.setText ("Pick a device from the list below first.", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                enabledToggle.setToggleState (false, juce::dontSendNotification);
                return;
            }
            const auto err = capture.setDevice (deviceCombo.getText());
            if (err.isNotEmpty())
            {
                statusLabel.setText ("Device error: " + err, juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                enabledToggle.setToggleState (false, juce::dontSendNotification);
                return;
            }
            refreshStatus();
        }

        void refreshStatus()
        {
            if (capture.isActive())
            {
                const auto lower = deviceCombo.getText().toLowerCase();
                const bool looksLikeLoopback = lower.contains ("monitor") || lower.contains ("loopback")
                                                 || lower.contains ("stereo mix") || lower.contains ("blackhole");
                if (looksLikeLoopback)
                {
                    statusLabel.setText ("Echoing system audio into your mic.", juce::dontSendNotification);
                    statusLabel.setColour (juce::Label::textColourId, palette().textDim);
                }
                else
                {
                    statusLabel.setText ("Active, but \"" + deviceCombo.getText() + "\" doesn't look like a "
                                           "loopback/monitor device, it may just be a mic and capture silence.",
                                           juce::dontSendNotification);
                    statusLabel.setColour (juce::Label::textColourId, palette().danger);
                }
            }
            else
            {
                statusLabel.setText ("Off, pick a loopback/monitor device and enable it to echo.",
                                       juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().textDim);
            }
        }

        void showInfo()
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Tab Echo",
                "Mixes whatever's coming out of a loopback/monitor INPUT DEVICE into your outgoing mic "
                "stream, e.g. so audio playing in another app gets echoed to whoever's listening.\n\n"
                "This is SYSTEM audio, not a surgically isolated single window or tab, close other "
                "noisy audio before relying on it.\n\n"
                "Linux: look for a device named like \"Monitor of ...\".\n"
                "Windows: look for \"(loopback)\".\n"
                "macOS: needs a virtual audio driver (e.g. BlackHole) installed.");
        }
    };
}
