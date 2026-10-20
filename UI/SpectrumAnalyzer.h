#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include "LookAndFeel.h"
#include <vector>

// Local FFT processing for spectrum display
namespace vlab::ui
{
    class SpectrumAnalyzer : public juce::Component
    {
    public:
        SpectrumAnalyzer() : fft (fftOrder)
        {
            window.resize (fftSize);
            for (int i = 0; i < fftSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
            fftBuf.assign ((size_t) fftSize * 2, 0.0f);
            bars.assign (kNumBars, 0.0f);
        }

        void setSamples (const std::vector<float>& samples)
        {
            if ((int) samples.size() < fftSize)
                return; // not enough for a full analysis window this tick, skip

            for (int i = 0; i < fftSize; ++i)
            {
                const int srcIdx = (int) samples.size() - fftSize + i; // most recent fftSize samples
                fftBuf[(size_t) i] = samples[(size_t) srcIdx] * window[(size_t) i];
            }
            std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (fftBuf.data());

            const int numBins = fftSize / 2;
            for (int bar = 0; bar < kNumBars; ++bar)
            {
                const double t0 = std::pow ((double) bar / kNumBars, 2.2);
                const double t1 = std::pow ((double) (bar + 1) / kNumBars, 2.2);
                const int bin0 = juce::jlimit (1, numBins - 1, (int) (t0 * numBins));
                const int bin1 = juce::jlimit (bin0 + 1, numBins, (int) (t1 * numBins));

                float mag = 0.0f;
                for (int b = bin0; b < bin1; ++b)
                {
                    const float re = fftBuf[(size_t) b * 2], im = fftBuf[(size_t) b * 2 + 1];
                    mag = std::max (mag, std::sqrt (re * re + im * im));
                }
                const float db = 20.0f * std::log10 (juce::jmax (1e-6f, mag / fftSize));
                const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); // -60dB..0dB -> 0..1

                bars[(size_t) bar] = norm > bars[(size_t) bar] ? norm : bars[(size_t) bar] * 0.85f + norm * 0.15f;
            }
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

            auto area = bounds.reduced (8.0f);
            const float barW = area.getWidth() / kNumBars;
            for (int i = 0; i < kNumBars; ++i)
            {
                const float h = bars[(size_t) i] * area.getHeight();
                juce::Rectangle<float> barRect (area.getX() + i * barW, area.getBottom() - h, barW * 0.8f, h);
                juce::ColourGradient barGrad (p.accent, 0, area.getBottom(), p.accent.brighter (0.4f), 0, area.getY(), false);
                g.setGradientFill (barGrad);
                g.fillRoundedRectangle (barRect, 1.5f);
            }
        }

    private:
        static constexpr int fftOrder = 10; // 1024
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int kNumBars = 48;

        juce::dsp::FFT fft;
        std::vector<float> window;
        std::vector<float> fftBuf;
        std::vector<float> bars;
    };
}
