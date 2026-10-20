#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include <vector>

namespace vlab::ui
{
    class Goniometer : public juce::Component
    {
    public:
        void setSamples (const std::vector<float>& left, const std::vector<float>& right)
        {
            const int n = juce::jmin ((int) left.size(), (int) right.size());
            points.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const float l = left[(size_t) i], r = right[(size_t) i];
                points[(size_t) i] = { (l - r) * 0.5f, (l + r) * 0.5f };
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

            auto area = bounds.reduced (10.0f);
            const auto centre = area.getCentre();
            const float scale = juce::jmin (area.getWidth(), area.getHeight()) * 0.48f;

            g.setColour (p.stroke.withAlpha (0.6f));
            g.drawVerticalLine ((int) centre.x, area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) centre.y, area.getX(), area.getRight());

            g.setColour (p.accent);
            for (auto& pt : points)
            {
                const float x = centre.x + juce::jlimit (-1.0f, 1.0f, pt.x) * scale;
                const float y = centre.y - juce::jlimit (-1.0f, 1.0f, pt.y) * scale;
                g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
            }

            g.setColour (p.textDim);
            g.setFont (juce::Font (10.0f));
            g.drawText ("MONO", juce::Rectangle<int> ((int) centre.x - 20, area.getBottom() - 14, 40, 12), juce::Justification::centred);
        }

    private:
        struct Pt { float x, y; };
        std::vector<Pt> points;
    };
}
