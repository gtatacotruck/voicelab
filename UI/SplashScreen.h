#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include "PongGame.h"

namespace vlab::ui
{
    enum class SplashStyle { Blue, Green, Red, Pink, Gray, Retro95, Purple, Orange, Cyan, AmberTerminal };

    class SplashScreen : public juce::Component, private juce::Timer
    {
    public:
        std::function<void()> onFinished;

        explicit SplashScreen (SplashStyle styleIn = SplashStyle::Blue) : style (styleIn)
        {
            isEasterEgg = juce::Random::getSystemRandom().nextInt (40) == 0;
            titleText = isEasterEgg ? "C LAB" : "V LAB";
            tagline = kTaglines[(size_t) juce::Random::getSystemRandom().nextInt ((int) kTaglines.size())];
            startMs = juce::Time::getMillisecondCounter();
            startTimerHz (60);
        }

        void reportLoadingStatus (const juce::String& label, int done, int total)
        {
            statusLabel = label;
            statusDone = done;
            statusTotal = total;
            if (total > 0)
                progress = juce::jlimit (0.0f, 1.0f, (float) done / (float) total);
            repaint();
        }

        void notifyLoadComplete() { loadComplete = true; }

        void paint (juce::Graphics& g) override
        {
            if (style == SplashStyle::Retro95)
                paintRetro95 (g, getLocalBounds().toFloat());
            else if (style == SplashStyle::AmberTerminal)
                paintAmberTerminal (g, getLocalBounds().toFloat());
            else
                paintModern (g, getLocalBounds().toFloat());
        }

        void resized() override
        {
            if (pong == nullptr) return;
            if (style == SplashStyle::Retro95)
                pong->setBounds (getLocalBounds().reduced (10).withTrimmedTop (52));
            else if (style == SplashStyle::AmberTerminal)
                pong->setBounds (getLocalBounds().reduced (34).withTrimmedTop (90));
            else
                pong->setBounds (getLocalBounds().reduced (24).withTrimmedTop (30));
        }

    private:
        SplashStyle style;

        struct Palette
        {
            juce::Colour bgCentre, bgEdge;
            juce::Colour glowInner, glowOuter;
            juce::Colour vLight, vDark;
            juce::Colour titleText, subText, taglineText;
            juce::Colour barTrack, barFillLo, barFillHi;
            juce::Colour stripBg, stripText;
        };

        static Palette paletteFor (SplashStyle s)
        {
            switch (s)
            {
                case SplashStyle::Green:
                    return { juce::Colour (0xff0a2a12), juce::Colour (0xff020805),
                             juce::Colour (0x5533ff66), juce::Colour (0x0033ff66),
                             juce::Colour (0xff8dffb0), juce::Colour (0xff2ecc55),
                             juce::Colour (0xffbfffcf), juce::Colour (0xff8fe0a8), juce::Colour (0xff559966),
                             juce::Colour (0xff0f2413), juce::Colour (0xff33cc66), juce::Colour (0xff8dffb0),
                             juce::Colour (0xff0a2a12), juce::Colour (0xff8dffb0) };
                case SplashStyle::Red:
                    return { juce::Colour (0xff3a0a0a), juce::Colour (0xff140202),
                             juce::Colour (0x55ff4d4d), juce::Colour (0x00ff4d4d),
                             juce::Colour (0xffff9d9d), juce::Colour (0xffe83030),
                             juce::Colour (0xffffd0d0), juce::Colour (0xffe89393), juce::Colour (0xffa85555),
                             juce::Colour (0xff3a1313), juce::Colour (0xffe83030), juce::Colour (0xffff8080),
                             juce::Colour (0xff3a0a0a), juce::Colour (0xffff9d9d) };
                case SplashStyle::Pink:
                    return { juce::Colour (0xff3a0a2a), juce::Colour (0xff140212),
                             juce::Colour (0x55ff6dc2), juce::Colour (0x00ff6dc2),
                             juce::Colour (0xffffb0e6), juce::Colour (0xffe83bb0),
                             juce::Colour (0xffffd8f0), juce::Colour (0xffe89bd0), juce::Colour (0xffa85590),
                             juce::Colour (0xff3a1330), juce::Colour (0xffe83bb0), juce::Colour (0xffffa0e0),
                             juce::Colour (0xff3a0a2a), juce::Colour (0xffffb0e6) };
                case SplashStyle::Gray:
                    return { juce::Colour (0xff2a2a2a), juce::Colour (0xff0a0a0a),
                             juce::Colour (0x55cccccc), juce::Colour (0x00cccccc),
                             juce::Colour (0xffe0e0e0), juce::Colour (0xff888888),
                             juce::Colour (0xffe8e8e8), juce::Colour (0xffb0b0b0), juce::Colour (0xff707070),
                             juce::Colour (0xff1c1c1c), juce::Colour (0xff888888), juce::Colour (0xffcccccc),
                             juce::Colour (0xff2a2a2a), juce::Colour (0xffe0e0e0) };
                case SplashStyle::Purple:
                    return { juce::Colour (0xff220a3a), juce::Colour (0xff0a0214),
                             juce::Colour (0x55a566ff), juce::Colour (0x00a566ff),
                             juce::Colour (0xffcdb0ff), juce::Colour (0xff8b3be8),
                             juce::Colour (0xffe4d8ff), juce::Colour (0xffbe9be0), juce::Colour (0xff7f55a8),
                             juce::Colour (0xff23133a), juce::Colour (0xff8b3be8), juce::Colour (0xffcdb0ff),
                             juce::Colour (0xff220a3a), juce::Colour (0xffcdb0ff) };
                case SplashStyle::Orange:
                    return { juce::Colour (0xff3a1c0a), juce::Colour (0xff140802),
                             juce::Colour (0x55ff9d4d), juce::Colour (0x00ff9d4d),
                             juce::Colour (0xffffcb9d), juce::Colour (0xffe87a30),
                             juce::Colour (0xffffdfc0), juce::Colour (0xffe8b393), juce::Colour (0xffa87d55),
                             juce::Colour (0xff3a2313), juce::Colour (0xffe87a30), juce::Colour (0xffffb880),
                             juce::Colour (0xff3a1c0a), juce::Colour (0xffffcb9d) };
                case SplashStyle::Cyan:
                    return { juce::Colour (0xff0a2a3a), juce::Colour (0xff020a14),
                             juce::Colour (0x554de1ff), juce::Colour (0x004de1ff),
                             juce::Colour (0xff9df0ff), juce::Colour (0xff30b0e8),
                             juce::Colour (0xffd0f5ff), juce::Colour (0xff93cee8), juce::Colour (0xff5590a8),
                             juce::Colour (0xff13313a), juce::Colour (0xff30b0e8), juce::Colour (0xff9df0ff),
                             juce::Colour (0xff0a2a3a), juce::Colour (0xff9df0ff) };
                case SplashStyle::Blue:
                case SplashStyle::Retro95:
                case SplashStyle::AmberTerminal:
                default:
                    return { juce::Colour (0xff0a1a3a), juce::Colour (0xff020814),
                             juce::Colour (0x552e8bff), juce::Colour (0x002e8bff),
                             juce::Colour (0xff7fd4ff), juce::Colour (0xff2e8bff),
                             juce::Colour (0xffbfe0ff), juce::Colour (0xff8fb8e8), juce::Colour (0xff5578a8),
                             juce::Colour (0xff13233f), juce::Colour (0xff2e8bff), juce::Colour (0xff7fd4ff),
                             juce::Colour (0xff0a1a3a), juce::Colour (0xff7fd4ff) };
            }
        }

        // ── Blue / Green / Red / Pink / Gray: one shared layout ────────────
        void paintModern (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const auto p = paletteFor (style);

            juce::ColourGradient bg (p.bgCentre, bounds.getCentreX(), bounds.getCentreY(),
                                      p.bgEdge, bounds.getX(), bounds.getY(), true);
            bg.addColour (0.55, p.bgCentre.interpolatedWith (p.bgEdge, 0.4f));
            g.setGradientFill (bg);
            g.fillAll();

            if (pong != nullptr)
            {
                paintLoadingStrip (g, bounds.removeFromTop (30.0f), p);
                if (boredToastAlpha > 0.0f)
                    paintBoredToast (g, bounds, p);
                return;
            }

            const float glowR = 180.0f;
            const float glowCy = bounds.getCentreY() - 40.0f;
            juce::ColourGradient glow (p.glowInner, bounds.getCentreX(), glowCy,
                                        p.glowOuter, bounds.getCentreX() - glowR, glowCy, true);
            g.setGradientFill (glow);
            g.fillEllipse (bounds.getCentreX() - glowR, glowCy - glowR, glowR * 2, glowR * 2);

            drawSpinningV (g, bounds.getCentreX(), glowCy, p);

            g.setColour (p.titleText);
            g.setFont (juce::Font (22.0f, juce::Font::bold));
            g.drawText (titleText, bounds.withY (glowCy + 62.0f).withHeight (30), juce::Justification::centred);

            g.setColour (p.subText);
            g.setFont (juce::Font (13.0f));
            g.drawText ("C++ Edition", bounds.withY (glowCy + 92.0f).withHeight (22), juce::Justification::centred);

            g.setColour (p.taglineText);
            g.setFont (juce::Font (12.0f, juce::Font::italic));
            juce::String belowTitle = statusLabel.isNotEmpty()
                ? (statusTotal > 0 ? (statusLabel + "... " + juce::String (statusDone) + "/" + juce::String (statusTotal))
                                    : (statusLabel + "..."))
                : tagline;
            g.drawFittedText (belowTitle, bounds.withY (glowCy + 118.0f).withHeight (36).reduced (40, 0).toNearestInt(),
                               juce::Justification::centredTop, 2);

            const float barW = 220.0f, barH = 3.0f;
            juce::Rectangle<float> barBounds (bounds.getCentreX() - barW / 2, glowCy + 166.0f, barW, barH);
            g.setColour (p.barTrack);
            g.fillRoundedRectangle (barBounds, barH / 2);
            juce::ColourGradient fillGrad (p.barFillLo, barBounds.getX(), 0, p.barFillHi, barBounds.getRight(), 0, false);
            g.setGradientFill (fillGrad);
            g.fillRoundedRectangle (barBounds.withWidth (barW * progress), barH / 2);
        }

        void paintLoadingStrip (juce::Graphics& g, juce::Rectangle<float> strip, const Palette& p)
        {
            g.setColour (p.stripBg);
            g.fillRect (strip);
            g.setColour (p.stripText);
            g.setFont (juce::Font (12.0f));
            juce::String text = statusLabel.isNotEmpty()
                ? (statusLabel + "... " + (statusTotal > 0 ? (juce::String (statusDone) + "/" + juce::String (statusTotal)) : juce::String())
                   + " ,  still loading, hang tight")
                : "Still loading, hang tight...";
            g.drawText (text, strip.toNearestInt(), juce::Justification::centred);
        }

        void paintBoredToast (juce::Graphics& g, juce::Rectangle<float> area, const Palette& p)
        {
            juce::String msg = "Bored? Play some Pong to pass the time!";
            g.setColour (p.bgCentre.withAlpha (0.9f * boredToastAlpha));
            auto toastBounds = area.withHeight (30.0f).reduced (area.getWidth() * 0.15f, 0.0f);
            g.fillRoundedRectangle (toastBounds, 8.0f);
            g.setColour (p.titleText.withAlpha (boredToastAlpha));
            g.setFont (juce::Font (13.0f, juce::Font::bold));
            g.drawText (msg, toastBounds, juce::Justification::centred);
        }

        void drawSpinningV (juce::Graphics& g, float cx, float cy, const Palette& p)
        {
            const float r = 46.0f;
            const juce::Point<float> centre (cx, cy);
            const juce::Point<float> tip (cx, cy + r * 0.75f);
            const juce::Point<float> left (cx - r, cy - r * 0.65f);
            const juce::Point<float> right (cx + r, cy - r * 0.65f);

            juce::Path v;
            v.startNewSubPath (left);
            v.lineTo (tip);
            v.lineTo (right);

            juce::PathStrokeType stroke (16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            auto strokedV = juce::Path();
            stroke.createStrokedPath (strokedV, v);

            auto transform = juce::AffineTransform::rotation (spinAngle, centre.x, centre.y);

            g.setColour (p.glowInner);
            g.fillPath (strokedV, transform.followedBy (juce::AffineTransform::scale (1.15f, 1.15f, centre.x, centre.y)));

            juce::ColourGradient vGrad (p.vLight, left.x, left.y, p.vDark, right.x, right.y, false);
            g.setGradientFill (vGrad);
            g.fillPath (strokedV, transform);

            for (int i = 1; i <= 3; ++i)
            {
                const float trailAngle = spinAngle - (float) i * 0.22f;
                g.setColour (p.vDark.withAlpha (0.12f / (float) i));
                g.fillPath (strokedV, juce::AffineTransform::rotation (trailAngle, centre.x, centre.y));
            }
        }

        // ── Retro95: a Windows-95-installer pastiche, own layout entirely ──
        static constexpr juce::uint32 kFace      = 0xffC0C0C0;
        static constexpr juce::uint32 kHighlight = 0xffFFFFFF;
        static constexpr juce::uint32 kShadow    = 0xff808080;
        static constexpr juce::uint32 kDarkEdge  = 0xff000000;
        static constexpr juce::uint32 kNavyLo    = 0xff000080;
        static constexpr juce::uint32 kNavyHi    = 0xff1084D0;

        void paintRetro95 (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.setColour (juce::Colour (0xff008080)); // teal desktop showing round the edges, classic Win95 default
            g.fillAll();

            auto win = bounds.reduced (6.0f);
            drawBevel (g, win, true);
            g.setColour (juce::Colour (kFace));
            g.fillRect (win.reduced (2.0f));

            auto titleBar = win.reduced (2.0f).removeFromTop (22.0f);
            juce::ColourGradient tbGrad (juce::Colour (kNavyLo), titleBar.getX(), 0,
                                          juce::Colour (kNavyHi), titleBar.getRight(), 0, false);
            g.setGradientFill (tbGrad);
            g.fillRect (titleBar);

            auto iconBox = titleBar.reduced (3.0f).removeFromLeft (16.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (iconBox);
            g.setColour (juce::Colour (kNavyLo));
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText ("V", iconBox.toNearestInt(), juce::Justification::centred);

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (12.5f, juce::Font::bold));
            g.drawText (titleText + " Setup", titleBar.withTrimmedLeft (24.0f).withTrimmedRight (44.0f),
                        juce::Justification::centredLeft);

            auto btnArea = titleBar.reduced (3.0f).removeFromRight (36.0f);
            drawTitleBarButton (g, btnArea.removeFromLeft (16.0f), false);
            btnArea.removeFromLeft (2.0f);
            drawTitleBarButton (g, btnArea, true);

            auto body = win.reduced (2.0f);
            body.removeFromTop (22.0f);
            body = body.reduced (14.0f, 12.0f);

            if (pong != nullptr)
            {
                g.setColour (juce::Colour (kFace));
                g.fillRect (body);
                paintRetroStatusLine (g, body.removeFromTop (18.0f));
                return;
            }

            auto iconArea = body.removeFromLeft (70.0f);
            auto sunken = iconArea.removeFromTop (64.0f).withSizeKeepingCentre (56.0f, 56.0f);
            drawBevel (g, sunken, false);
            g.setColour (juce::Colours::white);
            g.fillRect (sunken.reduced (2.0f));
            drawFlatV (g, sunken.getCentreX(), sunken.getCentreY());

            body.removeFromLeft (16.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (16.0f, juce::Font::bold));
            g.drawText (titleText, body.removeFromTop (24.0f), juce::Justification::centredLeft);

            g.setFont (juce::Font (12.0f));
            juce::String msg = statusLabel.isNotEmpty()
                ? (statusLabel + "..." + (statusTotal > 0 ? ("  (" + juce::String (statusDone) + "/" + juce::String (statusTotal) + ")") : juce::String()))
                : "Setup is preparing the installation.";
            g.drawFittedText (msg, body.removeFromTop (36.0f).toNearestInt(), juce::Justification::topLeft, 2);

            auto meterArea = win.reduced (2.0f);
            meterArea = meterArea.removeFromBottom (34.0f).reduced (14.0f, 8.0f);
            drawSegmentedMeter (g, meterArea);
        }

        void drawBevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised)
        {
            g.setColour (juce::Colour (kDarkEdge));
            g.drawRect (r, 1.0f);
            r = r.reduced (1.0f);
            g.setColour (juce::Colour (raised ? kHighlight : kShadow));
            g.fillRect (r.removeFromTop (1.0f));
            g.setColour (juce::Colour (raised ? kHighlight : kShadow));
            g.fillRect (r.removeFromLeft (1.0f));
            g.setColour (juce::Colour (raised ? kShadow : kHighlight));
            g.fillRect (r.removeFromBottom (1.0f));
            g.setColour (juce::Colour (raised ? kShadow : kHighlight));
            g.fillRect (r.removeFromRight (1.0f));
        }

        void drawTitleBarButton (juce::Graphics& g, juce::Rectangle<float> r, bool isClose)
        {
            g.setColour (juce::Colour (kFace));
            g.fillRect (r);
            drawBevel (g, r, true);
            g.setColour (juce::Colours::black);
            juce::Path mark;
            auto m = r.reduced (4.0f);
            if (isClose)
            {
                mark.addLineSegment ({ m.getTopLeft(), m.getBottomRight() }, 1.2f);
                mark.addLineSegment ({ m.getTopRight(), m.getBottomLeft() }, 1.2f);
            }
            else
            {
                mark.addLineSegment ({ m.getBottomLeft(), m.getBottomRight() }, 1.4f);
            }
            g.fillPath (mark);
        }

        void drawFlatV (juce::Graphics& g, float cx, float cy)
        {
            const float r = 16.0f;
            juce::Path v;
            v.startNewSubPath (cx - r, cy - r * 0.5f);
            v.lineTo (cx, cy + r * 0.7f);
            v.lineTo (cx + r, cy - r * 0.5f);
            juce::PathStrokeType stroke (6.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square);
            juce::Path strokedV;
            stroke.createStrokedPath (strokedV, v);
            g.setColour (juce::Colour (kNavyLo));
            g.fillPath (strokedV);
        }

        void drawSegmentedMeter (juce::Graphics& g, juce::Rectangle<float> area)
        {
            drawBevel (g, area, false);
            area = area.reduced (2.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (area);

            constexpr int kSegments = 24;
            const float segGap = 2.0f;
            const float segW = (area.getWidth() - segGap * (kSegments - 1)) / (float) kSegments;
            const int filled = (int) (progress * kSegments);

            for (int i = 0; i < kSegments; ++i)
            {
                auto seg = area.removeFromLeft ((int) segW).reduced (0.0f, 2.0f);
                if (i < filled)
                {
                    g.setColour (juce::Colour (kNavyLo));
                    g.fillRect (seg);
                }
                area.removeFromLeft (segGap);
            }
        }

        void paintRetroStatusLine (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (11.0f));
            juce::String text = statusLabel.isNotEmpty()
                ? (statusLabel + "...  still loading, hang tight")
                : "Still loading, hang tight...";
            g.drawText (text, r.toNearestInt(), juce::Justification::centredLeft);
        }

        // ── Amber Terminal: monochrome CRT, own layout entirely ────────────
        static constexpr juce::uint32 kAmber = 0xffFFB000;

        void paintAmberTerminal (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.fillAll (juce::Colours::black);

            auto term = bounds.reduced (18.0f);
            g.setColour (juce::Colour (kAmber).withAlpha (0.7f));
            g.drawRect (term, 1.5f);
            term = term.reduced (16.0f);

            const juce::Colour amber (kAmber);
            const juce::Font mono (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain);

            g.setColour (amber);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 30.0f, juce::Font::bold));
            g.drawText (titleText, term.removeFromTop (44.0f), juce::Justification::centredLeft);

            term.removeFromTop (4.0f);
            g.setColour (amber.withAlpha (0.75f));
            g.setFont (mono);
            g.drawText ("C++ EDITION", term.removeFromTop (22.0f), juce::Justification::centredLeft);

            term.removeFromTop (16.0f);

            if (pong != nullptr)
            {
                g.setColour (amber.withAlpha (0.85f));
                juce::String text = statusLabel.isNotEmpty()
                    ? (statusLabel + "... still loading, hang tight")
                    : "still loading, hang tight...";
                g.drawText ("> " + text, term.removeFromTop (22.0f), juce::Justification::centredLeft);
                drawScanlines (g, bounds);
                return;
            }

            g.setColour (amber.withAlpha (0.85f));
            juce::String belowTitle = statusLabel.isNotEmpty()
                ? (statusLabel + "..." + (statusTotal > 0 ? ("  " + juce::String (statusDone) + "/" + juce::String (statusTotal)) : juce::String()))
                : tagline;
            g.drawFittedText ("> " + belowTitle + (((ticks / 30) % 2 == 0) ? "_" : ""),
                               term.removeFromTop (44.0f).toNearestInt(), juce::Justification::topLeft, 2);

            term.removeFromTop (10.0f);

            constexpr int kBlocks = 36;
            const int filled = (int) (progress * kBlocks);
            juce::String bar = "[";
            for (int i = 0; i < kBlocks; ++i)
                bar << juce::String (i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91");
            bar << "]";
            g.setColour (amber);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
            g.drawText (bar, term.removeFromTop (24.0f), juce::Justification::centredLeft);

            drawScanlines (g, bounds);
        }

        void drawScanlines (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            for (float y = bounds.getY(); y < bounds.getBottom(); y += 3.0f)
                g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);

            const float flicker = 0.03f + 0.02f * std::sin ((float) ticks * 0.3f);
            g.setColour (juce::Colour (kAmber).withAlpha (flicker));
            g.fillRect (bounds);
        }

        juce::String titleText;
        juce::String tagline;
        bool isEasterEgg = false;
        float progress = 0.0f;
        float spinAngle = 0.0f;
        int ticks = 0;

        juce::String statusLabel;
        int statusDone = 0, statusTotal = 0;
        juce::uint32 startMs = 0;
        bool loadComplete = false;
        bool pongOffered = false;
        std::unique_ptr<PongGame> pong;
        float boredToastAlpha = 0.0f;
        int boredToastTicks = 0;

        static inline const std::array<const char*, 32> kTaglines = {
            "Now with 30% more knobs.",
            "Error:audio too good.",
            "Your voice, but weirder.",
            "Please do not feed the reverb.",
            "Loading... unlike your motivation.",
            "Warning: contains audio.",
            "Turning coffee into soundwaves.",
            "It's not a bug, it's a feature (probably).",
            "Do not microwave this application.",
            "Pitch shifting responsibly since today.",
            "100% organic, free-range bytes.",
            "Now leak-free! (the app, not your mic)",
            "Side effects may include better audio.",
            "Powered by spite and JUCE.",
            "Ctrl+Z does not work on real life.",
            "Reticulating splines",
            "May contain traces of C++.",
            "Your mic called. It said thanks.",
            "Insert witty loading message here.",
            "Percolating audio buffers...",
            "woah awsome presets dawg!",
            "Some assembly required (i did it for you).",
            "This splash screen is load-bearing.",
            "Now accepting suggestions and blame.",
            "Sounding good since compile time.",
            "Warming up the tube amps.",
            "Please hold, adjusting the flux capacitor.",
            "No animals were harmed in this render.",
            "Voice changer, personality not included. (yet)",
            "Loading jokes... this one wasn't very good.",
            "Legally distinct from other soundboards.",
            "Certified 100% artisanal audio pipeline."
        };

        void timerCallback() override
        {
            ++ticks;
            if (pong == nullptr)
                progress = juce::jmin (progress + 0.01f, statusTotal > 0 ? progress : 0.9f);
            spinAngle += 0.11f;
            if (spinAngle > juce::MathConstants<float>::twoPi)
                spinAngle -= juce::MathConstants<float>::twoPi;

            const juce::uint32 elapsedMs = juce::Time::getMillisecondCounter() - startMs;

            if (! loadComplete && pong == nullptr && elapsedMs > 5000 && ! pongOffered)
            {
                pongOffered = true;
                pong = std::make_unique<PongGame>();
                addAndMakeVisible (*pong);
                resized();
                boredToastAlpha = 1.0f;
                boredToastTicks = 0;
            }

            if (boredToastAlpha > 0.0f)
            {
                ++boredToastTicks;
                if (boredToastTicks > 150)
                    boredToastAlpha = juce::jmax (0.0f, boredToastAlpha - 0.07f);
            }

            repaint();

            if (loadComplete && ticks > 4)
            {
                stopTimer();
                if (onFinished) onFinished();
            }
        }
    };
}
