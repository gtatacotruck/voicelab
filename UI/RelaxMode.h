#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include "LookAndFeel.h"
#include <functional>
#include <vector>
#include <cmath>

// fullscreen visualizer, opened with Ctrl+Shift+V
namespace vlab::ui
{
    class RelaxOverlay : public juce::Component, private juce::Timer
    {
    public:
        std::function<void()> onExit;
        std::function<const std::vector<float>&()> getSamples;
        std::function<juce::String()> getNowPlaying;

        RelaxOverlay() : fft (kOrder)
        {
            window.resize (kSize);
            for (int i = 0; i < kSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kSize - 1));
            fftBuf.assign ((size_t) kSize * 2, 0.0f);
            bars.assign (kBars, 0.0f);
            peaks.assign (kBars, 0.0f);
            setWantsKeyboardFocus (true);
            setInterceptsMouseClicks (true, false);
        }

        void start()
        {
            ticks = 0;
            std::fill (bars.begin(), bars.end(), 0.0f);
            startTimerHz (40);
            grabKeyboardFocus();
        }

        void stop() { stopTimer(); }

        bool keyPressed (const juce::KeyPress& key) override
        {
            const bool isV = key.getTextCharacter() == 'v' || key.getTextCharacter() == 'V';
            if (key.getKeyCode() == juce::KeyPress::escapeKey || (isV && ! key.getModifiers().isCtrlDown()))
            {
                if (onExit) onExit();
                return true;
            }
            return false;
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            const auto& pal = palette();
            const float t = (float) ticks / 40.0f;

            // slow colour drift around the theme accent
            const float baseHue = pal.accent.getHue();
            auto bg1 = juce::Colour::fromHSV (std::fmod (baseHue + 0.02f * std::sin (t * 0.1f) + 1.0f, 1.0f), 0.55f, 0.12f, 1.0f);
            auto bg2 = juce::Colour::fromHSV (std::fmod (baseHue + 0.12f + 0.03f * std::sin (t * 0.07f) + 1.0f, 1.0f), 0.6f, 0.05f, 1.0f);
            g.setGradientFill (juce::ColourGradient (bg1, b.getCentreX(), b.getCentreY(), bg2, 0.0f, 0.0f, true));
            g.fillAll();

            for (int i = 0; i < 6; ++i)
            {
                const float fi = (float) i;
                const float x = b.getWidth() * (0.5f + 0.4f * std::sin (t * (0.05f + 0.013f * fi) + fi * 1.7f));
                const float y = b.getHeight() * (0.5f + 0.38f * std::cos (t * (0.04f + 0.011f * fi) + fi * 2.3f));
                const float r = juce::jmin (b.getWidth(), b.getHeight()) * (0.18f + 0.05f * std::sin (t * 0.2f + fi)) * (1.0f + energy * 0.6f);
                auto c = juce::Colour::fromHSV (std::fmod (baseHue + 0.08f * fi, 1.0f), 0.55f, 0.7f, 0.0f);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.10f + 0.10f * energy), x, y, c.withAlpha (0.0f), x + r, y, true));
                g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            }

            const auto centre = b.getCentre().translated (0.0f, -b.getHeight() * 0.06f);
            const float minDim = juce::jmin (b.getWidth(), b.getHeight());
            const float innerR = minDim * (0.14f + 0.02f * energy);
            const float maxR = minDim * 0.40f;

            g.setColour (pal.accent.withAlpha (0.25f));
            g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.5f);

            const float spin = t * 0.06f;
            for (int i = 0; i < kBars; ++i)
            {
                const float a = spin + (float) i / (float) kBars * juce::MathConstants<float>::twoPi;
                const float r2 = innerR + 6.0f + bars[(size_t) i] * (maxR - innerR);
                const juce::Point<float> p1 (centre.x + std::cos (a) * innerR, centre.y + std::sin (a) * innerR);
                const juce::Point<float> p2 (centre.x + std::cos (a) * r2, centre.y + std::sin (a) * r2);
                auto col = juce::Colour::fromHSV (std::fmod (baseHue + (float) i / (float) kBars * 0.25f, 1.0f), 0.5f, 1.0f, 1.0f);
                g.setGradientFill (juce::ColourGradient (col.withAlpha (0.35f), p1.x, p1.y, col.withAlpha (0.95f), p2.x, p2.y, false));
                g.drawLine ({ p1, p2 }, juce::jmax (2.0f, minDim * 0.006f));

                const float pr = innerR + 6.0f + peaks[(size_t) i] * (maxR - innerR) + 6.0f;
                g.setColour (col.withAlpha (0.5f));
                g.fillEllipse (centre.x + std::cos (a) * pr - 1.5f, centre.y + std::sin (a) * pr - 1.5f, 3.0f, 3.0f);
            }

            const float breathe = 0.65f + 0.35f * std::sin (t * 0.9f);
            g.setColour (juce::Colours::white.withAlpha (0.55f + 0.35f * breathe));
            g.setFont (juce::Font (juce::jlimit (18.0f, 38.0f, minDim * 0.045f)));
            g.drawText ("just chill out man (press v or esc to exit)",
                        getLocalBounds().withTrimmedTop ((int) (b.getHeight() * 0.80f)).withHeight ((int) (b.getHeight() * 0.1f)),
                        juce::Justification::centred);

            if (nowPlaying.isNotEmpty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.setFont (juce::Font (juce::jlimit (12.0f, 20.0f, minDim * 0.022f)));
                g.drawText (nowPlaying, getLocalBounds().withTrimmedTop ((int) (b.getHeight() * 0.90f)).withHeight ((int) (b.getHeight() * 0.06f)),
                            juce::Justification::centred);
            }
        }

    private:
        static constexpr int kOrder = 10, kSize = 1 << kOrder, kBars = 96;

        juce::dsp::FFT fft;
        std::vector<float> window, fftBuf, bars, peaks;
        juce::String nowPlaying;
        float energy = 0.0f;
        juce::int64 ticks = 0;

        void timerCallback() override
        {
            ++ticks;
            if (getSamples)
            {
                const auto& s = getSamples();
                if ((int) s.size() >= kSize)
                {
                    for (int i = 0; i < kSize; ++i)
                        fftBuf[(size_t) i] = s[s.size() - (size_t) kSize + (size_t) i] * window[(size_t) i];
                    std::fill (fftBuf.begin() + kSize, fftBuf.end(), 0.0f);
                    fft.performRealOnlyForwardTransform (fftBuf.data());

                    float sum = 0.0f;
                    for (int i = 0; i < kBars; ++i)
                    {
                        const int bin0 = juce::jlimit (1, kSize / 2 - 1, (int) std::pow ((float) i / kBars, 2.2f) * (kSize / 2));
                        const int bin1 = juce::jlimit (bin0 + 1, kSize / 2, (int) std::pow ((float) (i + 1) / kBars, 2.2f) * (kSize / 2));
                        float m = 0.0f;
                        for (int k = bin0; k < bin1; ++k)
                            m = juce::jmax (m, std::abs (fftBuf[(size_t) k]));
                        const float db = juce::Decibels::gainToDecibels (m / (float) kSize * 4.0f, -80.0f);
                        const float target = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                        bars[(size_t) i] += (target - bars[(size_t) i]) * (target > bars[(size_t) i] ? 0.6f : 0.12f);
                        peaks[(size_t) i] = juce::jmax (peaks[(size_t) i] - 0.012f, bars[(size_t) i]);
                        sum += bars[(size_t) i];
                    }
                    energy += (sum / (float) kBars - energy) * 0.15f;
                }
            }
            if (ticks % 20 == 0)
                nowPlaying = getNowPlaying ? getNowPlaying() : juce::String();
            repaint();
        }
    };
}
