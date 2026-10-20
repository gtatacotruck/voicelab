#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace vlab::ui
{
    struct Palette
    {
        juce::Colour bg          { 0xff0a0a0a };
        juce::Colour panel       { 0xff121212 };
        juce::Colour panelAlt    { 0xff181818 };
        juce::Colour stroke      { 0xff2a2a2a };
        juce::Colour accent      { 0xff2ee66b }; // primary green
        juce::Colour accentDim   { 0xff1d8a45 };
        juce::Colour accentGlow  { 0x552ee66b };
        juce::Colour text        { 0xffe8f5ec };
        juce::Colour textDim     { 0xff8fa398 };
        juce::Colour danger      { 0xffe6543f };
        juce::Colour accent2     { accent }; // defaults equal to accent, set it different to get a

        juce::Colour textHover;
        juce::Colour buttonHover;
        juce::Colour sliderTrack;
        juce::Colour sliderThumb;
        juce::Colour scrollbarThumb;
        juce::Colour tooltipBg;
        juce::Colour tooltipText;
        juce::Colour selectionBg;
        juce::Colour linkColour;
        juce::Colour focusRing;

        juce::String cursorStyle { "default" }; // default | hand | crosshair | ibeam | none | custom
        juce::String cursorImage;
        float cursorHotspotX = 0.0f;
        float cursorHotspotY = 0.0f;
        int cursorSize = 32;
    };

    inline Palette& palette() { static Palette p; return p; }

    inline void setPalette (const Palette& newPalette) { palette() = newPalette; }

    inline juce::Colour resolve (juce::Colour override_, juce::Colour fallback)
    {
        return override_.getAlpha() > 0 ? override_ : fallback;
    }

    struct UiPreset
    {
        float cornerRadius   = 10.0f;  // 0 = sharp/square, ~20 = very rounded/pill-like
        float borderWidth    = 1.0f;   // stroke thickness on panels/buttons
        bool  useGradients   = true;   // false = flat fills everywhere (minimalist look)
        float glowIntensity  = 1.0f;   // 0 = no glow effects, up to ~2 = strong glow
        float fontScale      = 1.0f;   // 0.8 - 1.3, scales most UI text

        bool  glassmorphism  = false;
        float glassOpacity   = 0.55f;  // 0-1, how see-through glass panels are (lower = more transparent)

        float accentGradientAngle = 90.0f;  // 0-360 degrees, direction of the accent/accent2 gradient
        bool  pillButtons         = false;  // true = buttons always fully round-ended, ignoring cornerRadius
        float shadowIntensity     = 0.0f;   // 0 = none, up to ~2 = a pronounced drop shadow under every panel
        bool  scanlines           = false;  // faint horizontal CRT-style scanlines over every panel
        float noiseAmount         = 0.0f;   // 0-1, subtle film-grain speckle over every panel
        bool  accentBorderGlow    = false;  // glow every panel's border in the accent colour, glass or not
        float vignette            = 0.0f;   // 0-1, darkens each panel's corners inward from its own edges
    };

    inline UiPreset& uiPreset() { static UiPreset u; return u; }

    inline void setUiPreset (const UiPreset& newPreset) { uiPreset() = newPreset; }

    inline juce::ColourGradient makeAngledGradient (juce::Rectangle<float> bounds, float angleDegrees,
                                                      juce::Colour from, juce::Colour to)
    {
        const float rad = juce::degreesToRadians (angleDegrees);
        const float halfDiag = 0.5f * std::sqrt (bounds.getWidth() * bounds.getWidth() + bounds.getHeight() * bounds.getHeight());
        const float dx = std::cos (rad) * halfDiag, dy = std::sin (rad) * halfDiag;
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        return juce::ColourGradient (from, cx - dx, cy - dy, to, cx + dx, cy + dy, false);
    }

    inline std::optional<juce::MouseCursor> resolveThemedCursor (const Palette& p, const juce::File& themesFolder)
    {
        if (p.cursorStyle == "hand")      return juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
        if (p.cursorStyle == "crosshair") return juce::MouseCursor (juce::MouseCursor::CrosshairCursor);
        if (p.cursorStyle == "ibeam")     return juce::MouseCursor (juce::MouseCursor::IBeamCursor);
        if (p.cursorStyle == "none")      return juce::MouseCursor (juce::MouseCursor::NoCursor);

        if (p.cursorStyle == "custom" && p.cursorImage.isNotEmpty())
        {
            const auto file = juce::File::isAbsolutePath (p.cursorImage)
                                   ? juce::File (p.cursorImage)
                                   : themesFolder.getChildFile (p.cursorImage);
            const auto loaded = juce::ImageFileFormat::loadFrom (file);
            if (loaded.isValid())
            {
                const int targetSize = juce::jmax (8, p.cursorSize);
                const int longestEdge = juce::jmax (loaded.getWidth(), loaded.getHeight());
                const float scale = longestEdge > 0 ? (float) targetSize / (float) longestEdge : 1.0f;
                const int w = juce::jmax (1, juce::roundToInt (loaded.getWidth() * scale));
                const int h = juce::jmax (1, juce::roundToInt (loaded.getHeight() * scale));

                juce::Image resized (loaded.getFormat(), w, h, true);
                juce::Graphics g (resized);
                g.drawImage (loaded, 0, 0, w, h, 0, 0, loaded.getWidth(), loaded.getHeight());

                return juce::MouseCursor (resized,
                    (int) (w * juce::jlimit (0.0f, 1.0f, p.cursorHotspotX)),
                    (int) (h * juce::jlimit (0.0f, 1.0f, p.cursorHotspotY)));
            }
        }

        return std::nullopt;
    }

    inline void drawPanelBackground (juce::Graphics& g, juce::Rectangle<float> bounds,
                                      const Palette& p, const UiPreset& u, float radius)
    {
        if (u.shadowIntensity > 0.0f)
        {
            auto shadowBounds = bounds.translated (0.0f, 2.0f + 2.5f * u.shadowIntensity).expanded (1.0f);
            g.setColour (juce::Colours::black.withAlpha (juce::jlimit (0.0f, 0.6f, 0.18f * u.shadowIntensity)));
            g.fillRoundedRectangle (shadowBounds, radius);
        }

        if (u.glassmorphism)
        {
            if (u.glowIntensity > 0.0f)
            {
                g.setColour (p.accentGlow.withAlpha (juce::jlimit (0.0f, 0.35f, 0.12f * u.glowIntensity)));
                g.fillRoundedRectangle (bounds.expanded (3.0f * u.glowIntensity), radius + 3.0f);
            }

            g.setColour (p.panel.withAlpha (juce::jlimit (0.05f, 1.0f, u.glassOpacity)));
            g.fillRoundedRectangle (bounds, radius);

            auto topHighlight = bounds.removeFromTop (juce::jmin (bounds.getHeight() * 0.4f, 40.0f));
            juce::ColourGradient highlight (juce::Colours::white.withAlpha (0.10f), topHighlight.getX(), topHighlight.getY(),
                                             juce::Colours::white.withAlpha (0.0f), topHighlight.getX(), topHighlight.getBottom(), false);
            g.setGradientFill (highlight);
            g.fillRoundedRectangle (topHighlight, radius);

            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (bounds.withY (bounds.getY() - topHighlight.getHeight()).withHeight (bounds.getHeight() + topHighlight.getHeight()).reduced (0.5f),
                                     radius, u.borderWidth);
        }
        else if (u.useGradients)
        {
            juce::ColourGradient bg (p.panel.brighter (0.02f), bounds.getX(), bounds.getY(),
                                      p.panel.darker (0.15f), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), radius, u.borderWidth);
        }
        else
        {
            g.setColour (p.panel);
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), radius, u.borderWidth);
        }

        if (u.accentBorderGlow)
        {
            g.setColour (p.accent.withAlpha (juce::jlimit (0.0f, 0.55f, 0.2f + 0.15f * u.glowIntensity)));
            g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.5f, 1.5f);
        }

        if (u.scanlines || u.noiseAmount > 0.0f || u.vignette > 0.0f)
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (bounds, radius);
            g.reduceClipRegion (clip);

            if (u.scanlines)
            {
                g.setColour (juce::Colours::black.withAlpha (0.10f));
                for (float ly = bounds.getY(); ly < bounds.getBottom(); ly += 3.0f)
                    g.drawLine (bounds.getX(), ly, bounds.getRight(), ly, 1.0f);
            }

            if (u.noiseAmount > 0.0f)
            {
                // Seeded from position/size for fixed texture (no flicker)
                juce::Random rnd ((juce::int64) (bounds.getX() * 733.0f + bounds.getY() * 977.0f + bounds.getWidth() * 131.0f));
                const int dots = juce::jlimit (0, 4000, (int) (bounds.getWidth() * bounds.getHeight() * 0.015f * u.noiseAmount));
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                for (int i = 0; i < dots; ++i)
                    g.fillRect (bounds.getX() + rnd.nextFloat() * bounds.getWidth(),
                                bounds.getY() + rnd.nextFloat() * bounds.getHeight(), 1.0f, 1.0f);
            }

            if (u.vignette > 0.0f)
            {
                juce::ColourGradient vg (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                          juce::Colours::black.withAlpha (juce::jlimit (0.0f, 0.85f, 0.6f * u.vignette)),
                                          bounds.getX(), bounds.getY(), true);
                g.setGradientFill (vg);
                g.fillRoundedRectangle (bounds, radius);
            }

            g.restoreState();
        }
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            applyPaletteColours();
        }

        void setThemesFolder (const juce::File& folder)
        {
            themesFolder = folder;
            refreshThemedCursor();
        }

        void applyPaletteColours()
        {
            const auto& p = palette();
            setColour (juce::ResizableWindow::backgroundColourId, p.bg);
            setColour (juce::TextButton::buttonColourId, p.panelAlt);
            setColour (juce::TextButton::textColourOffId, p.text);
            setColour (juce::TextButton::textColourOnId, p.bg);
            setColour (juce::ComboBox::backgroundColourId, p.panelAlt);
            setColour (juce::ComboBox::outlineColourId, p.accent);
            setColour (juce::ComboBox::textColourId, p.text);
            setColour (juce::ComboBox::arrowColourId, p.accent);
            setColour (juce::Slider::trackColourId, p.accentDim);
            setColour (juce::Slider::thumbColourId, p.accent);
            setColour (juce::Slider::backgroundColourId, p.panelAlt);
            setColour (juce::Label::textColourId, p.text);
            setColour (juce::TextEditor::backgroundColourId, p.panelAlt);
            setColour (juce::TextEditor::outlineColourId, p.stroke);
            setColour (juce::TextEditor::focusedOutlineColourId, resolve (p.focusRing, p.accent));
            setColour (juce::TextEditor::textColourId, p.text);
            setColour (juce::TextEditor::highlightColourId, resolve (p.selectionBg, p.accentDim.withAlpha (0.5f)));
            setColour (juce::ScrollBar::thumbColourId, resolve (p.scrollbarThumb, p.accentDim));
            setColour (juce::PopupMenu::backgroundColourId, p.panel);
            setColour (juce::PopupMenu::textColourId, p.text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accentDim);
            setColour (juce::PopupMenu::highlightedTextColourId, resolve (p.textHover, p.text));
            setColour (juce::TooltipWindow::backgroundColourId, resolve (p.tooltipBg, p.panelAlt));
            setColour (juce::TooltipWindow::textColourId, resolve (p.tooltipText, p.text));
            setColour (juce::TooltipWindow::outlineColourId, p.stroke);
            setColour (juce::HyperlinkButton::textColourId, resolve (p.linkColour, p.accent));

            refreshThemedCursor();
        }

        juce::MouseCursor getMouseCursorFor (juce::Component& c) override
        {
            auto def = juce::LookAndFeel_V4::getMouseCursorFor (c);
            if (themedCursor.has_value() && def == juce::MouseCursor (juce::MouseCursor::NormalCursor))
                return *themedCursor;
            return def;
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                    bool highlighted, bool down) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool primary = b.getProperties().getWithDefault ("primary", false);
            const bool danger  = b.getProperties().getWithDefault ("danger", false);
            const float radius = u.pillButtons ? bounds.getHeight() * 0.5f
                                                : juce::jmin (bounds.getHeight() * 0.5f, u.cornerRadius);

            const bool secondary = ! primary && ! b.getToggleState() && ! danger;
            const bool hasButtonHover = p.buttonHover.getAlpha() > 0;
            const bool useAccent2 = ! danger && (primary || b.getToggleState()) && p.accent2 != p.accent;
            juce::Colour top, bottom;
            if (danger)       { top = p.danger.brighter (0.15f); bottom = p.danger.darker (0.35f); }
            else if (primary || b.getToggleState()) { top = p.accent.brighter (0.25f); bottom = p.accent.darker (0.25f); }
            else if (secondary && highlighted && hasButtonHover) { top = p.buttonHover.brighter (0.05f); bottom = p.buttonHover.darker (0.08f); }
            else              { top = p.panelAlt.brighter (0.06f); bottom = p.panelAlt.darker (0.12f); }

            if (down)        { top = top.darker (0.2f); bottom = bottom.darker (0.2f); }
            else if (highlighted && ! (secondary && hasButtonHover)) { top = top.brighter (0.08f); bottom = bottom.brighter (0.04f); }

            if (useAccent2)
            {
                auto a2 = down ? p.accent2.darker (0.2f) : (highlighted ? p.accent2.brighter (0.08f) : p.accent2);
                g.setGradientFill (makeAngledGradient (bounds, u.accentGradientAngle, top, a2));
            }
            else if (u.useGradients)
            {
                juce::ColourGradient grad (top, bounds.getX(), bounds.getY(), bottom, bounds.getX(), bounds.getBottom(), false);
                g.setGradientFill (grad);
            }
            else
            {
                g.setColour (top); // flat-fill presets use just the lighter of the two tones
            }
            g.fillRoundedRectangle (bounds, radius);

            if (! primary && ! b.getToggleState() && ! danger)
            {
                g.setColour (p.stroke);
                g.drawRoundedRectangle (bounds, radius, u.borderWidth);
            }
            else
            {
                g.setColour ((danger ? p.danger : p.accent).withAlpha (0.5f));
                g.drawRoundedRectangle (bounds, radius, u.borderWidth);
            }
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) override
        {
            const auto& p = palette();
            const bool primary = b.getProperties().getWithDefault ("primary", false);
            const bool danger  = b.getProperties().getWithDefault ("danger", false);
            const bool onColouredFill = primary || danger || b.getToggleState();
            g.setColour (onColouredFill ? p.bg : (highlighted ? resolve (p.textHover, p.text) : p.text));
            g.setFont (juce::Font (14.0f * uiPreset().fontScale, juce::Font::bold));
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& t, bool highlighted, bool) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            const float h = (float) t.getHeight();
            const float switchW = 38.0f, switchH = juce::jmin (h, 20.0f);
            juce::Rectangle<float> track (0, (h - switchH) / 2.0f, switchW, switchH);

            juce::Colour onCol = p.accent, offCol = p.panelAlt;
            juce::Colour trackTop = t.getToggleState() ? onCol.brighter (0.2f) : offCol.brighter (0.05f);
            juce::Colour trackBot = t.getToggleState() ? onCol.darker (0.2f) : offCol.darker (0.1f);
            if (u.useGradients)
            {
                juce::ColourGradient grad (trackTop, track.getX(), track.getY(), trackBot, track.getX(), track.getBottom(), false);
                g.setGradientFill (grad);
            }
            else
            {
                g.setColour (trackTop);
            }
            g.fillRoundedRectangle (track, switchH / 2.0f);
            if (! t.getToggleState())
            {
                g.setColour (p.stroke);
                g.drawRoundedRectangle (track, switchH / 2.0f, u.borderWidth);
            }

            const float knobR = switchH - 4.0f;
            const float knobX = t.getToggleState() ? track.getRight() - knobR - 2.0f : track.getX() + 2.0f;
            g.setColour (juce::Colours::white.withAlpha (highlighted ? 1.0f : 0.92f));
            g.fillEllipse (knobX, track.getY() + 2.0f, knobR, knobR);

            g.setColour (highlighted ? resolve (p.textHover, p.text) : p.text);
            g.setFont (juce::Font (13.0f * u.fontScale));
            g.drawFittedText (t.getButtonText(),
                juce::Rectangle<int> ((int) switchW + 10, 0, t.getWidth() - (int) switchW - 10, t.getHeight()),
                juce::Justification::centredLeft, 1);
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                            int, int, int, int, juce::ComboBox& box) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
            const float radius = juce::jmin (u.cornerRadius, bounds.getHeight() * 0.5f);

            if (u.useGradients)
            {
                juce::ColourGradient grad (p.panelAlt.brighter (0.05f), bounds.getX(), bounds.getY(),
                                            p.panelAlt.darker (0.1f), bounds.getX(), bounds.getBottom(), false);
                g.setGradientFill (grad);
            }
            else
            {
                g.setColour (p.panelAlt);
            }
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (box.hasKeyboardFocus (true) ? p.accent : p.accent.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds, radius, juce::jmax (0.6f, u.borderWidth * 1.2f));

            juce::Path arrow;
            const float ax = width - 18.0f, ay = height * 0.5f;
            arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
            g.setColour (p.accent);
            g.fillPath (arrow);
        }

        void drawLabel (juce::Graphics& g, juce::Label& l) override
        {
            g.setColour (l.findColour (juce::Label::textColourId));
            auto font = getLabelFont (l);
            font.setHeight (font.getHeight() * uiPreset().fontScale);
            g.setFont (font);
            g.drawFittedText (l.getText(), l.getLocalBounds(), l.getJustificationType(), 1);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                float sliderPos, float, float, juce::Slider::SliderStyle,
                                juce::Slider& s) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            const float trackY = y + height * 0.5f;
            const float trackH = 4.0f;
            juce::Rectangle<float> track ((float) x, trackY - trackH / 2, (float) width, trackH);
            g.setColour (resolve (p.sliderTrack, p.panelAlt));
            g.fillRoundedRectangle (track, trackH / 2);

            juce::Rectangle<float> fill ((float) x, trackY - trackH / 2, juce::jmax (0.0f, sliderPos - x), trackH);
            const bool useAccent2 = p.accent2 != p.accent;
            if (useAccent2)
                g.setGradientFill (makeAngledGradient (fill.isEmpty() ? track : fill, u.accentGradientAngle, p.accentDim, p.accent2));
            else if (u.useGradients)
            {
                juce::ColourGradient fillGrad (p.accentDim, fill.getX(), 0, p.accent, fill.getRight(), 0, false);
                g.setGradientFill (fillGrad);
            }
            else
            {
                g.setColour (p.accent);
            }
            g.fillRoundedRectangle (fill, trackH / 2);

            const juce::Colour thumbColour = resolve (p.sliderThumb, p.accent);
            const float r = 7.0f;
            if (u.glowIntensity > 0.0f)
            {
                g.setColour ((useAccent2 ? p.accent2 : thumbColour).withAlpha (juce::jlimit (0.0f, 0.6f, 0.25f * u.glowIntensity)));
                const float glowR = r + 3.0f * u.glowIntensity;
                g.fillEllipse (sliderPos - glowR, trackY - glowR, glowR * 2, glowR * 2);
            }
            if (useAccent2)
            {
                juce::ColourGradient thumbGrad (juce::Colours::white.withAlpha (0.9f), sliderPos - r, trackY - r,
                                                 p.accent2, sliderPos + r, trackY + r, true);
                g.setGradientFill (thumbGrad);
            }
            else if (u.useGradients)
            {
                juce::ColourGradient thumbGrad (juce::Colours::white.withAlpha (0.9f), sliderPos - r, trackY - r,
                                                 thumbColour, sliderPos + r, trackY + r, true);
                g.setGradientFill (thumbGrad);
            }
            else
            {
                g.setColour (thumbColour);
            }
            g.fillEllipse (sliderPos - r, trackY - r, r * 2, r * 2);
            g.setColour (p.bg);
            g.drawEllipse (sliderPos - r, trackY - r, r * 2, r * 2, 1.2f);
        }

    private:
        juce::File themesFolder;
        std::optional<juce::MouseCursor> themedCursor;

        void refreshThemedCursor()
        {
            themedCursor = resolveThemedCursor (palette(), themesFolder);
        }
    };
}
