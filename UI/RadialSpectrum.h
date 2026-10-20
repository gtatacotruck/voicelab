#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include "LookAndFeel.h"
#include <vector>
#include <cmath>

namespace vlab::ui
{
    class RadialSpectrum : public juce::Component
    {
    public:
        RadialSpectrum() : fft (fftOrder)
        {
            window.resize (fftSize);
            for (int i = 0; i < fftSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
            fftBuf.assign ((size_t) fftSize * 2, 0.0f);
            bars.assign (kNumSpokes, 0.0f);
        }

        void setSamples (const std::vector<float>& samples)
        {
            if ((int) samples.size() < fftSize)
                return;

            for (int i = 0; i < fftSize; ++i)
            {
                const int srcIdx = (int) samples.size() - fftSize + i;
                fftBuf[(size_t) i] = samples[(size_t) srcIdx] * window[(size_t) i];
            }
            std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (fftBuf.data());

            const int numBins = fftSize / 2;
            for (int spoke = 0; spoke < kNumSpokes; ++spoke)
            {
                const double t0 = std::pow ((double) spoke / kNumSpokes, 2.2);
                const double t1 = std::pow ((double) (spoke + 1) / kNumSpokes, 2.2);
                const int bin0 = juce::jlimit (1, numBins - 1, (int) (t0 * numBins));
                const int bin1 = juce::jlimit (bin0 + 1, numBins, (int) (t1 * numBins));

                float mag = 0.0f;
                for (int b = bin0; b < bin1; ++b)
                {
                    const float re = fftBuf[(size_t) b * 2], im = fftBuf[(size_t) b * 2 + 1];
                    mag = std::max (mag, std::sqrt (re * re + im * im));
                }
                const float db = 20.0f * std::log10 (juce::jmax (1e-6f, mag / fftSize));
                const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

                bars[(size_t) spoke] = norm > bars[(size_t) spoke] ? norm : bars[(size_t) spoke] * 0.85f + norm * 0.15f;
            }
            ++ticks;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat();
            juce::ColourGradient bg (p.panel.brighter (0.02f), 0, 0, p.panel.darker (0.15f), 0, bounds.getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (bounds, u.cornerRadius);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), u.cornerRadius, u.borderWidth);

            auto area = bounds.reduced (16.0f);
            const auto centre = area.getCentre();
            const float innerR = juce::jmin (area.getWidth(), area.getHeight()) * 0.16f;
            const float maxR   = juce::jmin (area.getWidth(), area.getHeight()) * 0.48f;

            g.setColour (p.stroke.withAlpha (0.5f));
            g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

            const float slowSpin = (float) ticks * 0.15f; // gentle rotation, purely decorative

            for (int i = 0; i < kNumSpokes; ++i)
            {
                const float angle = slowSpin + (float) i / (float) kNumSpokes * juce::MathConstants<float>::twoPi;
                const float r1 = innerR;
                const float r2 = innerR + bars[(size_t) i] * (maxR - innerR);

                const juce::Point<float> p1 (centre.x + std::cos (angle) * r1, centre.y + std::sin (angle) * r1);
                const juce::Point<float> p2 (centre.x + std::cos (angle) * r2, centre.y + std::sin (angle) * r2);

                juce::ColourGradient grad (p.accent, p1.x, p1.y, p.accent.brighter (0.5f), p2.x, p2.y, false);
                g.setGradientFill (grad);
                g.drawLine ({ p1, p2 }, 3.0f);
            }

            g.setColour (p.accentDim.withAlpha (0.6f));
            g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
        }

    private:
        static constexpr int fftOrder = 10; // 1024
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int kNumSpokes = 64;

        juce::dsp::FFT fft;
        std::vector<float> window;
        std::vector<float> fftBuf;
        std::vector<float> bars;
        int64_t ticks = 0;
    };
}
