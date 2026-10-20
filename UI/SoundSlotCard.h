#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "../Soundboard/SoundSlot.h"

namespace vlab::ui
{
    class SoundSlotCard : public juce::Component, private juce::ChangeListener
    {
    public:
        std::function<void()> onPlay, onStop, onLoad, onBind, onUnbind, onDelete;
        std::function<void (juce::Colour)> onColourPicked;
        std::function<void (float)> onSeek; // fraction 0..1
        std::function<void (vlab::PlayMode)> onSetPlayMode;

        int getSlotIndex() const { return index; }

        explicit SoundSlotCard (int slotIndex) : index (slotIndex)
        {
            addAndMakeVisible (playButton);
            playButton.onClick = [this] { if (empty) { if (onLoad) onLoad(); } else if (onPlay) onPlay(); };

            for (auto* b : { &loadBtn, &stopBtn, &bindBtn, &colourBtn, &playModeBtn, &deleteBtn })
                addAndMakeVisible (*b);

            loadBtn.setTooltip ("Load a sound file into this slot");
            stopBtn.setTooltip ("Stop this slot if it's playing");
            colourBtn.setTooltip ("Pick a colour for this card");
            deleteBtn.setTooltip ("Clear this slot");

            loadBtn.onClick   = [this] { if (onLoad) onLoad(); };
            stopBtn.onClick   = [this] { if (onStop) onStop(); };
            bindBtn.onClick   = [this]
            {
                const auto now = juce::Time::getMillisecondCounter();
                if (lastBindClickMs != 0 && now - lastBindClickMs < 400)
                {
                    lastBindClickMs = 0;
                    if (onUnbind) onUnbind();
                }
                else
                {
                    lastBindClickMs = now;
                    if (onBind) onBind();
                }
            };
            deleteBtn.onClick = [this] { if (onDelete) onDelete(); };
            playModeBtn.onClick = [this]
            {
                currentPlayMode = cycledPlayMode (currentPlayMode);
                applyPlayModeToButton();
                if (onSetPlayMode) onSetPlayMode (currentPlayMode);
            };
            colourBtn.onClick = [this]
            {
                auto* selector = new juce::ColourSelector();
                selector->setSize (200, 200);
                selector->setCurrentColour (cardColour);
                selector->addChangeListener (this);
                juce::CallOutBox::launchAsynchronously (std::unique_ptr<Component> (selector),
                    colourBtn.getScreenBounds(), nullptr);
            };
        }

        void setSlot (const SoundSlot* slotPtr) { slot = slotPtr; refreshFromSlot(); }

        void pollProgress()
        {
            if (scrubbing)
                return; // avoid visual fighting between the drag and the lagging real value
            const float newProgress = (slot != nullptr) ? slot->progress : 0.0f;
            const bool wasPlaying = playing;
            playing = newProgress > 0.0f && newProgress < 100.0f;
            if (std::abs (newProgress - progress) > 0.5f || playing != wasPlaying)
            {
                progress = newProgress;
                playButton.setPlaying (playing);
                repaint();
            }
        }

        void refreshFromSlot()
        {
            if (slot == nullptr || (slot->path.empty() && slot->data == nullptr))
            {
                empty = true;
                displayName = "Empty";
            }
            else
            {
                empty = false;
                displayName = slot->name.empty() ? "Untitled" : slot->name;
                if (slot->hasColour) cardColour = slot->colour;
            }
            playButton.setEmpty (empty);

            const juce::String hotkeyText = (slot != nullptr) ? juce::String (slot->hotkey) : juce::String();
            bindBtn.active = hotkeyText.isNotEmpty();
            bindBtn.setTooltip (hotkeyText.isNotEmpty()
                ? ("Hotkey: " + hotkeyText.toUpperCase() + ", double-tap to remove, single-tap to rebind")
                : "Click, then press a key to bind a hotkey to this slot");

            currentPlayMode = (slot != nullptr) ? slot->playMode : vlab::PlayMode::Overlap;
            applyPlayModeToButton();

            colourBtn.swatchColour = cardColour;

            repaint();
        }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override { trySeek (e); }
        void mouseDrag (const juce::MouseEvent& e) override { trySeek (e); }
        void mouseUp (const juce::MouseEvent&) override { scrubbing = false; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);

            {
                juce::DropShadow shadow (juce::Colours::black.withAlpha (hovering ? 0.55f : 0.35f),
                                          hovering ? 14 : 8, { 0, hovering ? 5 : 3 });
                juce::Path cardPath;
                cardPath.addRoundedRectangle (bounds, 14.0f);
                shadow.drawForPath (g, cardPath);
            }

            if (u.glassmorphism)
            {
                g.setColour ((hovering ? p.panel.brighter (0.06f) : p.panel).withAlpha (juce::jlimit (0.05f, 1.0f, u.glassOpacity)));
                g.fillRoundedRectangle (bounds, u.cornerRadius);
                auto topHighlight = bounds.withHeight (juce::jmin (bounds.getHeight() * 0.4f, 24.0f));
                juce::ColourGradient highlight (juce::Colours::white.withAlpha (0.10f), topHighlight.getX(), topHighlight.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), topHighlight.getX(), topHighlight.getBottom(), false);
                g.setGradientFill (highlight);
                g.fillRoundedRectangle (topHighlight, u.cornerRadius);
            }
            else
            {
                juce::ColourGradient bg (hovering ? p.panel.brighter (0.06f) : p.panel.brighter (0.02f),
                                          bounds.getX(), bounds.getY(),
                                          p.panel.darker (0.18f), bounds.getX(), bounds.getBottom(), false);
                g.setGradientFill (bg);
                g.fillRoundedRectangle (bounds, u.cornerRadius);
            }

            // "Now playing" glow ring around the whole card.
            if (playing)
            {
                g.setColour (cardColour.withAlpha (juce::jlimit (0.0f, 1.0f, 0.5f * u.glowIntensity)));
                g.drawRoundedRectangle (bounds.reduced (0.5f), u.cornerRadius, 2.0f * juce::jmax (0.1f, u.glowIntensity));
            }
            g.setColour (empty ? p.stroke : cardColour.withAlpha (playing ? 0.9f : 0.6f));
            g.drawRoundedRectangle (bounds.reduced (playing ? 1.5f : 0.5f), u.cornerRadius, u.borderWidth * 1.2f);

            if (! empty)
            {
                juce::Rectangle<float> tab (bounds.getX() + 12, bounds.getY() + 10, 7, 7);
                g.setColour (cardColour);
                g.fillEllipse (tab);
            }

            g.setColour (empty ? p.textDim : p.text);
            g.setFont (juce::Font (13.0f, juce::Font::bold));
            auto nameArea = bounds.reduced (14.0f, 8.0f).removeFromTop (20.0f);
            if (! empty) nameArea.removeFromLeft (14.0f);
            g.drawFittedText (displayName, nameArea.toNearestInt(), juce::Justification::centredLeft, 1);

            if (! empty)
            {
                auto bar = barGeometry();
                if (hovering || scrubbing)
                    bar = bar.withSizeKeepingCentre (bar.getWidth(), 6.0f); // fatten up when interactive
                g.setColour (p.panelAlt);
                g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
                if (progress > 0.0f)
                {
                    juce::ColourGradient barGrad (cardColour.brighter (0.2f), bar.getX(), 0, cardColour, bar.getRight(), 0, false);
                    g.setGradientFill (barGrad);
                    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress / 100.0f), bar.getHeight() * 0.5f);
                }
                if (scrubbing)
                {
                    const float knobX = bar.getX() + bar.getWidth() * progress / 100.0f;
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (knobX - 4.0f, bar.getCentreY() - 4.0f, 8.0f, 8.0f);
                }
            }
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (12);
            bounds.removeFromTop (20); // name is painted directly above, not a child component

            auto playArea = bounds.removeFromTop (36);
            playButton.setBounds (playArea);

            bounds.removeFromTop (8);
            auto iconRow = bounds.removeFromTop (22);
            const int w = iconRow.getWidth() / 6;
            loadBtn.setBounds     (iconRow.removeFromLeft (w).reduced (2));
            stopBtn.setBounds     (iconRow.removeFromLeft (w).reduced (2));
            bindBtn.setBounds     (iconRow.removeFromLeft (w).reduced (2));
            colourBtn.setBounds   (iconRow.removeFromLeft (w).reduced (2));
            playModeBtn.setBounds (iconRow.removeFromLeft (w).reduced (2));
            deleteBtn.setBounds   (iconRow.removeFromLeft (w).reduced (2));
        }

    private:
        // Overlap -> Retrigger -> Stop Others -> back to Overlap.
        static vlab::PlayMode cycledPlayMode (vlab::PlayMode m)
        {
            switch (m)
            {
                case vlab::PlayMode::Overlap:    return vlab::PlayMode::Retrigger;
                case vlab::PlayMode::Retrigger:  return vlab::PlayMode::StopOthers;
                case vlab::PlayMode::StopOthers: default: return vlab::PlayMode::Overlap;
            }
        }

        void applyPlayModeToButton()
        {
            playModeBtn.active = (currentPlayMode != vlab::PlayMode::Overlap);
            const char* desc = currentPlayMode == vlab::PlayMode::Retrigger  ? "Retrigger (restarts itself, doesn't stack)"
                              : currentPlayMode == vlab::PlayMode::StopOthers ? "Stop Others (solo, stops every other sound)"
                              : "Overlap (default, can play on top of itself and others)";
            playModeBtn.setTooltip (juce::String ("Playback mode: ") + desc + ". Click to cycle.");
            repaint();
        }

        int index;
        const SoundSlot* slot = nullptr;
        bool empty = true;
        bool playing = false;
        bool hovering = false;
        bool scrubbing = false;
        float progress = 0.0f;
        juce::uint32 lastBindClickMs = 0;
        vlab::PlayMode currentPlayMode = vlab::PlayMode::Overlap;
        std::string displayName = "Empty";
        juce::Colour cardColour = palette().accent;

        juce::Rectangle<float> barGeometry() const
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            auto barArea = bounds.reduced (14.0f, 0.0f);
            return { barArea.getX(), bounds.getBottom() - 30.0f, barArea.getWidth(), 3.0f };
        }

        void trySeek (const juce::MouseEvent& e)
        {
            if (empty)
                return;
            const auto bar = barGeometry();
            const auto hitArea = bar.expanded (0.0f, 6.0f); // generous vertical hit area around the thin visual bar
            if (! hitArea.contains (e.position))
                return;

            scrubbing = true;
            const float fraction = juce::jlimit (0.0f, 1.0f, (e.position.x - bar.getX()) / juce::jmax (1.0f, bar.getWidth()));
            progress = fraction * 100.0f; // optimistic immediate visual feedback, corrected by the next pollProgress()
            repaint();
            if (onSeek) onSeek (fraction);
        }

        struct PlayButton : public juce::Button
        {
            PlayButton() : juce::Button ({}) {}

            void setEmpty (bool e) { isEmptySlot = e; repaint(); }
            void setPlaying (bool p) { isPlaying = p; repaint(); }

            void paintButton (juce::Graphics& g, bool highlighted, bool down) override
            {
                const auto& p = palette();
                auto b = getLocalBounds().toFloat().reduced (0.5f);
                const float radius = b.getHeight() * 0.5f;

                juce::Colour top = isEmptySlot ? p.panelAlt.brighter (0.08f) : p.accent.brighter (0.25f);
                juce::Colour bot = isEmptySlot ? p.panelAlt.darker (0.1f)   : p.accent.darker (0.2f);
                if (down) { top = top.darker (0.2f); bot = bot.darker (0.2f); }
                else if (highlighted) { top = top.brighter (0.08f); bot = bot.brighter (0.04f); }

                juce::ColourGradient grad (top, b.getX(), b.getY(), bot, b.getX(), b.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (b, radius);

                if (isEmptySlot)
                {
                    g.setColour (p.stroke);
                    g.drawRoundedRectangle (b, radius, 1.0f);
                }
                else if (isPlaying)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawRoundedRectangle (b, radius, 1.5f);
                }

                // Icon: folder (empty) / play triangle or pause bars (loaded)
                const float iconSize = b.getHeight() * 0.32f;
                const auto c = b.getCentreY();
                const float iconX = b.getX() + b.getWidth() * 0.5f - (isEmptySlot ? iconSize * 0.6f : iconSize * 0.35f);

                g.setColour (isEmptySlot ? p.accent : p.bg);
                if (isEmptySlot)
                {
                    juce::Path folder;
                    folder.addRoundedRectangle (iconX, c - iconSize * 0.4f, iconSize * 1.3f, iconSize * 0.8f, 1.5f);
                    g.strokePath (folder, juce::PathStrokeType (1.6f));
                }
                else if (isPlaying)
                {
                    // pause bars
                    g.fillRoundedRectangle (iconX, c - iconSize * 0.5f, iconSize * 0.35f, iconSize, 1.0f);
                    g.fillRoundedRectangle (iconX + iconSize * 0.55f, c - iconSize * 0.5f, iconSize * 0.35f, iconSize, 1.0f);
                }
                else
                {
                    juce::Path tri;
                    tri.addTriangle (iconX, c - iconSize * 0.55f, iconX, c + iconSize * 0.55f, iconX + iconSize * 0.9f, c);
                    g.fillPath (tri);
                }

                g.setFont (juce::Font (13.0f, juce::Font::bold));
                g.setColour (isEmptySlot ? p.text : p.bg);
                auto textArea = b.withTrimmedLeft (iconSize * 1.6f);
                g.drawFittedText (isEmptySlot ? "Load" : (isPlaying ? "Playing" : "Play"),
                                   textArea.toNearestInt(), juce::Justification::centredLeft, 1);
            }

            bool isEmptySlot = true, isPlaying = false;
        };

        PlayButton playButton;

        struct GlyphButton : public juce::Button
        {
            enum Glyph { Folder, Stop, Keyboard, Palette, Cross, Layers } glyph;
            bool active = false; // e.g. "this slot has a hotkey bound", draws a small accent dot
            juce::Colour swatchColour = palette().accent; // used by the Palette glyph to show the card's actual colour
            explicit GlyphButton (Glyph g) : juce::Button ({}), glyph (g) {}
            void paintButton (juce::Graphics& g, bool highlighted, bool down) override
            {
                const auto& p = palette();
                auto b = getLocalBounds().toFloat();
                g.setColour (down ? p.panelAlt.darker() : (highlighted ? p.panelAlt.brighter (0.1f) : p.panelAlt));
                g.fillRoundedRectangle (b, 6.0f);
                g.setColour (active ? p.accent : p.stroke);
                g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, active ? 1.4f : 1.0f);

                auto ic = b.reduced (b.getWidth() * 0.22f);
                g.setColour (glyph == Cross ? p.danger : (glyph == Palette ? swatchColour : p.accent));
                juce::Path path;
                switch (glyph)
                {
                    case Folder:
                        path.addRoundedRectangle (ic.getX(), ic.getY() + ic.getHeight() * 0.2f,
                                                   ic.getWidth(), ic.getHeight() * 0.7f, 1.5f);
                        g.strokePath (path, juce::PathStrokeType (1.4f));
                        break;
                    case Stop:
                        g.fillRoundedRectangle (ic, 1.5f);
                        break;
                    case Keyboard:
                        path.addRoundedRectangle (ic, 1.5f);
                        g.strokePath (path, juce::PathStrokeType (1.4f));
                        for (int i = 0; i < 3; ++i)
                            g.fillEllipse (ic.getX() + 2 + i * (ic.getWidth() / 3.2f), ic.getCentreY() - 1, 2, 2);
                        break;
                    case Palette:
                        g.fillEllipse (ic);
                        break;
                    case Cross:
                        g.drawLine (ic.getX(), ic.getY(), ic.getRight(), ic.getBottom(), 1.6f);
                        g.drawLine (ic.getX(), ic.getBottom(), ic.getRight(), ic.getY(), 1.6f);
                        break;
                    case Layers:
                    {
                        auto back = ic.withSizeKeepingCentre (ic.getWidth() * 0.72f, ic.getHeight() * 0.72f)
                                       .translated (-ic.getWidth() * 0.14f, -ic.getHeight() * 0.14f);
                        auto front = ic.withSizeKeepingCentre (ic.getWidth() * 0.72f, ic.getHeight() * 0.72f)
                                        .translated (ic.getWidth() * 0.14f, ic.getHeight() * 0.14f);
                        g.setColour (p.accent.withAlpha (0.55f));
                        path.addRoundedRectangle (back, 1.5f);
                        g.strokePath (path, juce::PathStrokeType (1.3f));
                        path.clear();
                        g.setColour (p.accent);
                        path.addRoundedRectangle (front, 1.5f);
                        g.strokePath (path, juce::PathStrokeType (1.3f));
                        break;
                    }
                }

                if (active)
                {
                    g.setColour (p.accent);
                    g.fillEllipse (b.getRight() - 6.0f, b.getY() + 1.0f, 5.0f, 5.0f);
                }
            }
        };

        GlyphButton loadBtn   { GlyphButton::Folder };
        GlyphButton stopBtn   { GlyphButton::Stop };
        GlyphButton bindBtn   { GlyphButton::Keyboard };
        GlyphButton colourBtn { GlyphButton::Palette };
        GlyphButton playModeBtn { GlyphButton::Layers };
        GlyphButton deleteBtn { GlyphButton::Cross };

        void changeListenerCallback (juce::ChangeBroadcaster* src) override
        {
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
            {
                cardColour = cs->getCurrentColour();
                if (onColourPicked) onColourPicked (cardColour);
                repaint();
            }
        }
    };
}

