#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"

namespace vlab::ui
{
    class VirtualHeadWidget : public juce::Component
    {
    public:
        std::function<void (float azimuthDeg)> onAzimuthChanged;

        void setAzimuth (float deg) { azimuth = deg; repaint(); }
        float getAzimuth() const { return azimuth; }

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

            const auto centre = bounds.getCentre();
            const float headR = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.22f;

            g.setColour (p.stroke.withAlpha (0.5f));
            g.drawEllipse (centre.x - headR * 1.9f, centre.y - headR * 1.9f, headR * 3.8f, headR * 3.8f, 1.0f);

            juce::ColourGradient headGrad (juce::Colour (0xff3a4a42), centre.x, centre.y - headR,
                                            juce::Colour (0xff1c2620), centre.x, centre.y + headR, false);
            g.setGradientFill (headGrad);
            g.fillEllipse (centre.x - headR, centre.y - headR, headR * 2, headR * 2);
            g.setColour (p.stroke);
            g.drawEllipse (centre.x - headR, centre.y - headR, headR * 2, headR * 2, 1.2f);

            juce::Path nose;
            nose.addTriangle (centre.x - headR * 0.18f, centre.y - headR * 0.95f,
                               centre.x + headR * 0.18f, centre.y - headR * 0.95f,
                               centre.x, centre.y - headR * 1.35f);
            g.setColour (p.accent.withAlpha (0.7f));
            g.fillPath (nose);

            g.setColour (p.textDim);
            g.fillEllipse (centre.x - headR * 1.15f, centre.y - headR * 0.25f, headR * 0.3f, headR * 0.5f);
            g.fillEllipse (centre.x + headR * 0.85f, centre.y - headR * 0.25f, headR * 0.3f, headR * 0.5f);

            g.setFont (juce::Font (10.0f));
            g.setColour (p.textDim);
            g.drawText ("L", (int) (centre.x - headR * 2.3f), (int) (centre.y - 7), 16, 14, juce::Justification::centred);
            g.drawText ("R", (int) (centre.x + headR * 1.5f), (int) (centre.y - 7), 16, 14, juce::Justification::centred);

            const float rad = juce::degreesToRadians (azimuth);
            const float orbitR = headR * 1.9f;
            const auto dot = centre.getPointOnCircumference (orbitR, orbitR, rad - juce::MathConstants<float>::halfPi);

            g.setColour (p.accent.withAlpha (0.3f));
            g.fillEllipse (dot.x - 10, dot.y - 10, 20, 20);
            juce::ColourGradient dotGrad (juce::Colours::white, dot.x - 6, dot.y - 6, p.accent, dot.x + 6, dot.y + 6, true);
            g.setGradientFill (dotGrad);
            g.fillEllipse (dot.x - 6, dot.y - 6, 12, 12);

            g.setColour (p.text);
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            const juce::String degText = juce::String ((int) azimuth) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            g.drawText (degText, bounds.removeFromBottom (18).toNearestInt(), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }

    private:
        float azimuth = 0.0f;

        void updateFromMouse (const juce::MouseEvent& e)
        {
            const auto centre = getLocalBounds().toFloat().getCentre();
            const float dx = e.position.x - centre.x;
            const float dy = e.position.y - centre.y;
            float deg = juce::radiansToDegrees (std::atan2 (dx, -dy));
            azimuth = juce::jlimit (-180.0f, 180.0f, deg);
            repaint();
            if (onAzimuthChanged) onAzimuthChanged (azimuth);
        }
    };
}
