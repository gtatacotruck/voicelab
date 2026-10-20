#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include <vector>
#include <cmath>

namespace vlab::ui
{
    class VuMeterDisplay : public juce::Component
    {
    public:
        void setSamples (const std::vector<float>& left, const std::vector<float>& right)
        {
            updateChannel (left, 0);
            updateChannel (right, 1);
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

            auto area = bounds.reduced (24.0f, 16.0f);
            const float gap = 18.0f;
            const float barW = (area.getWidth() - gap) * 0.5f;

            static const char* labels[2] = { "L", "R" };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto barArea = juce::Rectangle<float> (area.getX() + ch * (barW + gap), area.getY(), barW, area.getHeight());

                g.setColour (p.stroke.withAlpha (0.5f));
                g.drawRoundedRectangle (barArea, 3.0f, 1.0f);

                const float lvl = juce::jlimit (0.0f, 1.0f, level[(size_t) ch]);
                const float fillH = lvl * barArea.getHeight();
                auto fillRect = barArea.withTop (barArea.getBottom() - fillH);

                juce::ColourGradient grad (p.accent, 0, barArea.getBottom(),
                                            lvl > 0.85f ? p.danger : p.accent.brighter (0.4f), 0, barArea.getY(), false);
                grad.addColour (0.75, p.accent.brighter (0.2f));
                g.setGradientFill (grad);
                g.fillRoundedRectangle (fillRect.reduced (2.0f), 2.0f);

                const float peakY = barArea.getBottom() - juce::jlimit (0.0f, 1.0f, peak[(size_t) ch]) * barArea.getHeight();
                g.setColour (p.text);
                g.fillRect (barArea.getX() + 2.0f, peakY - 1.5f, barArea.getWidth() - 4.0f, 2.5f);

                g.setColour (p.textDim);
                g.setFont (juce::Font (12.0f, juce::Font::bold));
                g.drawText (labels[ch], barArea.withY (barArea.getBottom() + 2.0f).withHeight (14.0f),
                             juce::Justification::centred);
            }
        }

    private:
        void updateChannel (const std::vector<float>& samples, int ch)
        {
            if (samples.empty())
                return;

            double sumSq = 0.0;
            for (float s : samples)
                sumSq += (double) s * (double) s;
            const float rms = (float) std::sqrt (sumSq / (double) samples.size());

            level[(size_t) ch] = rms > level[(size_t) ch] ? rms : level[(size_t) ch] * 0.90f + rms * 0.10f;

            if (level[(size_t) ch] >= peak[(size_t) ch])
                peak[(size_t) ch] = level[(size_t) ch];
            else
                peak[(size_t) ch] = juce::jmax (0.0f, peak[(size_t) ch] - 0.006f); // slow peak-hold fall-off
        }

        float level[2] { 0.0f, 0.0f };
        float peak[2]  { 0.0f, 0.0f };
    };
}
