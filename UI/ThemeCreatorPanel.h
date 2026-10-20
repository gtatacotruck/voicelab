#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "ThemeManager.h"
#include <array>

namespace vlab::ui
{
    class ThemeCreatorPanel : public juce::Component, private juce::ChangeListener
    {
    public:
        std::function<void()> onThemeSaved; // fire so the caller can refresh other theme dropdowns

        ThemeCreatorPanel (ThemeManager& themeManagerIn, juce::File themesFolderIn)
            : themeManager (themeManagerIn), themesFolder (std::move (themesFolderIn))
        {
            addAndMakeVisible (titleLabel);
            titleLabel.setText ("Theme Creator", juce::dontSendNotification);
            titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, palette().accent);

            addAndMakeVisible (subtitleLabel);
            subtitleLabel.setText (
                "Edit every colour in the app's theme by hand, click a swatch to pick visually, "
                "or type/paste a hex code directly. Changes preview live across the whole app. "
                "Save when you're happy; it's written as a normal theme mod file.",
                juce::dontSendNotification);
            subtitleLabel.setFont (juce::Font (12.0f));
            subtitleLabel.setColour (juce::Label::textColourId, palette().textDim);
            subtitleLabel.setJustificationType (juce::Justification::topLeft);

            addAndMakeVisible (baseLabel);
            baseLabel.setText ("Start from:", juce::dontSendNotification);
            baseLabel.setFont (juce::Font (12.0f));
            baseLabel.setColour (juce::Label::textColourId, palette().textDim);

            addAndMakeVisible (baseThemeCombo);
            refreshBaseThemeList();
            baseThemeCombo.onChange = [this]
            {
                Palette p;
                if (themeManager.getPaletteByName (baseThemeCombo.getText(), p))
                {
                    working = p;
                    pushWorkingToRows();
                    previewLive();
                }
            };

            addAndMakeVisible (nameLabel);
            nameLabel.setText ("Theme name:", juce::dontSendNotification);
            nameLabel.setFont (juce::Font (12.0f));
            nameLabel.setColour (juce::Label::textColourId, palette().textDim);
            addAndMakeVisible (nameEditor);
            nameEditor.setTextToShowWhenEmpty ("My Custom Theme", palette().textDim);

            // Every core colour gets a row: label, swatch, hex box.
            addRow ("Background", &Palette::bg);
            addRow ("Panel", &Palette::panel);
            addRow ("Panel (Alt)", &Palette::panelAlt);
            addRow ("Stroke / Border", &Palette::stroke);
            addRow ("Accent", &Palette::accent);
            addRow ("Accent (Dim)", &Palette::accentDim);
            addRow ("Accent Glow (semi-transparent)", &Palette::accentGlow);
            addRow ("Text", &Palette::text);
            addRow ("Text (Dim)", &Palette::textDim);
            addRow ("Danger / Alert", &Palette::danger);
            addRow ("Accent 2 (gradient partner, same as Accent = off)", &Palette::accent2);

            addAndMakeVisible (rolesLabel);
            rolesLabel.setText ("Hover & role colours (leave a swatch's alpha at 0 to leave it unset):", juce::dontSendNotification);
            rolesLabel.setFont (juce::Font (13.0f, juce::Font::bold));
            rolesLabel.setColour (juce::Label::textColourId, palette().accent);

            addRow ("Text Hover", &Palette::textHover);
            addRow ("Button Hover", &Palette::buttonHover);
            addRow ("Slider Track", &Palette::sliderTrack);
            addRow ("Slider Thumb", &Palette::sliderThumb);
            addRow ("Scrollbar", &Palette::scrollbarThumb);
            addRow ("Tooltip Background", &Palette::tooltipBg);
            addRow ("Tooltip Text", &Palette::tooltipText);
            addRow ("Selection", &Palette::selectionBg);
            addRow ("Link", &Palette::linkColour);
            addRow ("Focus Ring", &Palette::focusRing);

            addAndMakeVisible (cursorLabel);
            cursorLabel.setText ("Cursor:", juce::dontSendNotification);
            cursorLabel.setFont (juce::Font (13.0f, juce::Font::bold));
            cursorLabel.setColour (juce::Label::textColourId, palette().accent);

            addAndMakeVisible (cursorStyleCombo);
            cursorStyleCombo.addItem ("Default", 1);
            cursorStyleCombo.addItem ("Hand", 2);
            cursorStyleCombo.addItem ("Crosshair", 3);
            cursorStyleCombo.addItem ("I-Beam", 4);
            cursorStyleCombo.addItem ("None", 5);
            cursorStyleCombo.addItem ("Custom Image...", 6);
            cursorStyleCombo.onChange = [this]
            {
                working.cursorStyle = cursorStyleIdToKey (cursorStyleCombo.getSelectedId());
                updateCursorRowVisibility();
                previewLive();
            };

            addChildComponent (chooseCursorImageButton);
            chooseCursorImageButton.onClick = [this]
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Choose a cursor image", juce::File(), "*.png");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc)
                    {
                        auto f = fc.getResult();
                        if (! f.existsAsFile()) return;
                        working.cursorImage = f.getFullPathName();
                        chooseCursorImageButton.setButtonText (f.getFileName());
                        previewLive();
                    });
            };

            addAdvancedSlider (cursorHotspotXLabel, cursorHotspotXSlider, "Hotspot X (0-1)", 0.0, 1.0,
                [this] (double v) { working.cursorHotspotX = (float) v; previewLive(); });
            addAdvancedSlider (cursorHotspotYLabel, cursorHotspotYSlider, "Hotspot Y (0-1)", 0.0, 1.0,
                [this] (double v) { working.cursorHotspotY = (float) v; previewLive(); });
            addAdvancedSlider (cursorSizeLabel, cursorSizeSlider, "Cursor Size (px)", 8.0, 128.0,
                [this] (double v) { working.cursorSize = (int) v; previewLive(); });

            addAndMakeVisible (saveButton);
            saveButton.getProperties().set ("primary", true);
            saveButton.onClick = [this] { saveTheme(); };

            addAndMakeVisible (revertButton);
            revertButton.getProperties().set ("danger", true);
            revertButton.onClick = [this]
            {
                themeManager.getPaletteByName (themeManager.getLastAppliedThemeName(), working);
                pushWorkingToRows();
                previewLive();
            };

            addAndMakeVisible (statusLabel);
            statusLabel.setFont (juce::Font (11.5f));
            statusLabel.setColour (juce::Label::textColourId, palette().textDim);

            // Start editing from whatever theme is currently active.
            themeManager.getPaletteByName (themeManager.getLastAppliedThemeName(), working);
            pushWorkingToRows();
        }

        void refreshBaseThemeList()
        {
            baseThemeCombo.clear();
            auto names = themeManager.getThemeNames();
            for (int i = 0; i < names.size(); ++i)
                baseThemeCombo.addItem (names[i], i + 1);
        }

        void resized() override
        {
            auto a = getLocalBounds().reduced (4);
            titleLabel.setBounds (a.removeFromTop (28));
            subtitleLabel.setBounds (a.removeFromTop (50));
            a.removeFromTop (10);

            auto baseRow = a.removeFromTop (28);
            baseLabel.setBounds (baseRow.removeFromLeft (90));
            baseThemeCombo.setBounds (baseRow);
            a.removeFromTop (10);

            auto nameRow = a.removeFromTop (28);
            nameLabel.setBounds (nameRow.removeFromLeft (90));
            nameEditor.setBounds (nameRow);
            a.removeFromTop (14);

            for (auto& row : rows)
            {
                auto r = a.removeFromTop (36);
                row.label.setBounds (r.removeFromLeft (180));
                row.swatch.setBounds (r.removeFromLeft (48).reduced (2));
                r.removeFromLeft (8);
                row.hexEditor.setBounds (r.removeFromLeft (juce::jmin (140, r.getWidth())));
                a.removeFromTop (6);

                if (&row == &rows[10])
                {
                    a.removeFromTop (6);
                    rolesLabel.setBounds (a.removeFromTop (20));
                    a.removeFromTop (6);
                }
            }

            a.removeFromTop (10);
            cursorLabel.setBounds (a.removeFromTop (20));
            a.removeFromTop (4);

            auto cursorRow = a.removeFromTop (28);
            cursorStyleCombo.setBounds (cursorRow.removeFromLeft (180));
            cursorRow.removeFromLeft (8);
            chooseCursorImageButton.setBounds (cursorRow);
            a.removeFromTop (6);

            if (cursorHotspotXSlider.isVisible())
            {
                auto hx = a.removeFromTop (28);
                cursorHotspotXLabel.setBounds (hx.removeFromLeft (140));
                cursorHotspotXSlider.setBounds (hx);
                a.removeFromTop (6);
                auto hy = a.removeFromTop (28);
                cursorHotspotYLabel.setBounds (hy.removeFromLeft (140));
                cursorHotspotYSlider.setBounds (hy);
                a.removeFromTop (6);
                auto hs = a.removeFromTop (28);
                cursorSizeLabel.setBounds (hs.removeFromLeft (140));
                cursorSizeSlider.setBounds (hs);
                a.removeFromTop (6);
            }
            a.removeFromTop (10);

            auto btnRow = a.removeFromTop (34);
            saveButton.setBounds (btnRow.removeFromLeft (btnRow.getWidth() * 2 / 3 - 4));
            btnRow.removeFromLeft (8);
            revertButton.setBounds (btnRow);
            a.removeFromTop (8);
            statusLabel.setBounds (a.removeFromTop (18));
        }

        int getPreferredHeight() const
        {
            const int hotspotRows = cursorHotspotXSlider.isVisible() ? 3 * 34 : 0;
            return 28 + 50 + 10 + 28 + 10 + 28 + 14
                 + (int) rows.size() * 42 + 26          // rows, plus the "Hover & role colours" heading
                 + 10 + 20 + 4 + 28 + 6 + hotspotRows + 10
                 + 34 + 8 + 18 + 20;
        }

    private:
        ThemeManager& themeManager;
        juce::File themesFolder;
        Palette working;

        juce::Label titleLabel, subtitleLabel, baseLabel, nameLabel, statusLabel;
        juce::ComboBox baseThemeCombo;
        juce::TextEditor nameEditor;
        juce::TextButton saveButton { "Save Theme" }, revertButton { "Revert to Current" };

        struct Row
        {
            juce::Label label;
            juce::TextButton swatch;
            juce::TextEditor hexEditor;
            juce::Colour Palette::* member = nullptr;
        };
        std::array<Row, 21> rows;
        int nextRow = 0;

        juce::Label rolesLabel;
        juce::Label cursorLabel;
        juce::ComboBox cursorStyleCombo;
        juce::TextButton chooseCursorImageButton { "Choose Cursor Image..." };
        juce::Label cursorHotspotXLabel, cursorHotspotYLabel, cursorSizeLabel;
        juce::Slider cursorHotspotXSlider, cursorHotspotYSlider, cursorSizeSlider;
        std::unique_ptr<juce::FileChooser> fileChooser;

        static juce::String cursorStyleIdToKey (int id)
        {
            switch (id)
            {
                case 2: return "hand";
                case 3: return "crosshair";
                case 4: return "ibeam";
                case 5: return "none";
                case 6: return "custom";
                default: return "default";
            }
        }

        static int cursorStyleKeyToId (const juce::String& key)
        {
            if (key == "hand") return 2;
            if (key == "crosshair") return 3;
            if (key == "ibeam") return 4;
            if (key == "none") return 5;
            if (key == "custom") return 6;
            return 1;
        }

        void updateCursorRowVisibility()
        {
            const bool custom = working.cursorStyle == "custom";
            chooseCursorImageButton.setVisible (custom);
            cursorHotspotXLabel.setVisible (custom);
            cursorHotspotYLabel.setVisible (custom);
            cursorHotspotXSlider.setVisible (custom);
            cursorHotspotYSlider.setVisible (custom);
            cursorSizeLabel.setVisible (custom);
            cursorSizeSlider.setVisible (custom);
            resized();
        }

        void addAdvancedSlider (juce::Label& label, juce::Slider& slider, const juce::String& text,
                                 double lo, double hi, std::function<void (double)> onChange)
        {
            addAndMakeVisible (label);
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (12.5f));
            label.setColour (juce::Label::textColourId, palette().text);

            addAndMakeVisible (slider);
            slider.setRange (lo, hi);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            slider.onValueChange = [&slider, onChange] { onChange (slider.getValue()); };
        }

        Row* activeEditRow = nullptr;

        void addRow (const juce::String& labelText, juce::Colour Palette::* member)
        {
            auto& row = rows[(size_t) nextRow++];
            row.member = member;

            addAndMakeVisible (row.label);
            row.label.setText (labelText, juce::dontSendNotification);
            row.label.setFont (juce::Font (12.5f));
            row.label.setColour (juce::Label::textColourId, palette().text);

            addAndMakeVisible (row.swatch);
            row.swatch.onClick = [this, &row] { openSwatchPicker (row); };

            addAndMakeVisible (row.hexEditor);
            row.hexEditor.setFont (juce::Font (12.0f));
            row.hexEditor.onReturnKey = [this, &row] { applyHexEdit (row); };
            row.hexEditor.onFocusLost = [this, &row] { applyHexEdit (row); };
        }

        void pushWorkingToRows()
        {
            for (auto& row : rows)
            {
                const juce::Colour c = working.*(row.member);
                row.swatch.setColour (juce::TextButton::buttonColourId, c);
                row.hexEditor.setText ("#" + c.toDisplayString (true).toLowerCase(), juce::dontSendNotification);
            }
            cursorStyleCombo.setSelectedId (cursorStyleKeyToId (working.cursorStyle), juce::dontSendNotification);
            chooseCursorImageButton.setButtonText (working.cursorImage.isNotEmpty()
                ? juce::File (working.cursorImage).getFileName() : juce::String ("Choose Cursor Image..."));
            cursorHotspotXSlider.setValue (working.cursorHotspotX, juce::dontSendNotification);
            cursorHotspotYSlider.setValue (working.cursorHotspotY, juce::dontSendNotification);
            cursorSizeSlider.setValue (working.cursorSize, juce::dontSendNotification);
            updateCursorRowVisibility();
            repaint();
        }

        void applyHexEdit (Row& row)
        {
            auto hex = row.hexEditor.getText().trim();
            if (hex.startsWithChar ('#')) hex = hex.substring (1);
            if (hex.length() == 6) hex = "ff" + hex;
            if (hex.length() != 8 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
            {
                statusLabel.setText ("Invalid hex colour, use #RRGGBB or #AARRGGBB", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                row.hexEditor.setText ("#" + (working.*(row.member)).toDisplayString (true).toLowerCase(),
                                        juce::dontSendNotification);
                return;
            }
            const auto c = juce::Colour::fromString (hex);
            working.*(row.member) = c;
            row.swatch.setColour (juce::TextButton::buttonColourId, c);
            statusLabel.setText ({}, juce::dontSendNotification);
            previewLive();
        }

        void openSwatchPicker (Row& row)
        {
            activeEditRow = &row;
            auto* selector = new juce::ColourSelector (
                juce::ColourSelector::showAlphaChannel | juce::ColourSelector::showColourAtTop
                | juce::ColourSelector::showSliders);
            selector->setSize (300, 380);
            selector->setCurrentColour (working.*(row.member));
            selector->addChangeListener (this);
            juce::CallOutBox::launchAsynchronously (std::unique_ptr<Component> (selector),
                row.swatch.getScreenBounds(), nullptr);
        }

        void changeListenerCallback (juce::ChangeBroadcaster* src) override
        {
            if (activeEditRow == nullptr) return;
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
            {
                const auto c = cs->getCurrentColour();
                working.*(activeEditRow->member) = c;
                activeEditRow->swatch.setColour (juce::TextButton::buttonColourId, c);
                activeEditRow->hexEditor.setText ("#" + c.toDisplayString (true).toLowerCase(), juce::dontSendNotification);
                previewLive();
            }
        }

        void previewLive() { themeManager.previewPalette (working); repaintEverything(); }

        void repaintEverything()
        {
            if (auto* tl = getTopLevelComponent())
                tl->repaint();
        }

        void saveTheme()
        {
            const auto name = nameEditor.getText().trim();
            if (name.isEmpty())
            {
                statusLabel.setText ("Give your theme a name first.", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                return;
            }

            auto file = themeManager.saveThemeToFile (name, working, themesFolder);
            if (! file.existsAsFile())
            {
                statusLabel.setText ("Could not save theme file.", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, palette().danger);
                return;
            }

            themeManager.applyTheme (name);
            repaintEverything();
            refreshBaseThemeList();
            statusLabel.setText ("Saved: " + file.getFileName(), juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, palette().accent);
            if (onThemeSaved) onThemeSaved();
        }
    };
}
