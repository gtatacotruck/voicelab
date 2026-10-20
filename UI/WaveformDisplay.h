#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include <vector>

namespace vlab::ui
{
    class WaveformDisplay : public juce::Component
    {
    public:
        void setSamples (const std::vector<float>& samples)
        {
            data = samples;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat();
            drawPanelBackground (g, bounds, p, u, u.cornerRadius);

            g.setColour (p.stroke.withAlpha (0.6f));
            g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX() + 8, bounds.getRight() - 8);

            if (data.empty())
                return;

            juce::Path path;
            const float midY = bounds.getCentreY();
            const float halfH = bounds.getHeight() * 0.42f;
            const float w = bounds.getWidth() - 16.0f;

            const int n = (int) data.size();
            for (int i = 0; i < n; ++i)
            {
                const float x = 8.0f + w * (float) i / (float) juce::jmax (1, n - 1);
                const float y = midY - juce::jlimit (-1.0f, 1.0f, data[(size_t) i]) * halfH;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }

            juce::Path fillPath = path;
            fillPath.lineTo (bounds.getRight() - 8, midY);
            fillPath.lineTo (bounds.getX() + 8, midY);
            fillPath.closeSubPath();
            g.setColour (p.accent.withAlpha (0.15f));
            g.fillPath (fillPath);

            juce::ColourGradient strokeGrad (p.accent.brighter (0.3f), bounds.getX(), 0, p.accent.darker (0.1f), bounds.getRight(), 0, false);
            g.setGradientFill (strokeGrad);
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }

    private:
        std::vector<float> data;
    };
}
