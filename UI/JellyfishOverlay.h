#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

namespace vlab::ui
{
    class JellyfishOverlay : public juce::Component, private juce::Timer
    {
    public:
        JellyfishOverlay()
        {
            setInterceptsMouseClicks (true, false);
            startTimerHz (30);
        }

        void spawnOne()
        {
            auto b = getLocalBounds().toFloat();
            if (b.isEmpty())
                return;

            Jellyfish j;
            j.pos = { b.getX() + rng.nextFloat() * b.getWidth(),
                      b.getY() + b.getHeight() * (0.5f + rng.nextFloat() * 0.5f) }; // start in the lower half
            const float angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            j.vel = { std::cos (angle) * 14.0f, std::sin (angle) * 14.0f - 10.0f }; // gentle initial drift, biased upward
            j.size = 26.0f + rng.nextFloat() * 22.0f;
            j.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            j.pulseSpeed = 1.6f + rng.nextFloat() * 0.8f;
            j.hue = rng.nextFloat();
            jellies.push_back (j);
            repaint();
        }

        void clearAll() { jellies.clear(); repaint(); }
        int getCount() const { return (int) jellies.size(); }

        bool hitTest (int x, int y) override
        {
            for (auto& j : jellies)
                if (j.bellBounds().contains ((float) x, (float) y))
                    return true;
            return false;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.position;
            for (int i = (int) jellies.size(); --i >= 0;) // topmost (last drawn) first
            {
                if (jellies[(size_t) i].bellBounds().contains (p))
                {
                    jellies.erase (jellies.begin() + i);
                    repaint();
                    return;
                }
            }
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& j : jellies)
                drawJellyfish (g, j);
        }

    private:
        struct Jellyfish
        {
            juce::Point<float> pos, vel;
            float size = 30.0f;
            float phase = 0.0f;      // drives the bell pulse + tentacle sway
            float pulseSpeed = 2.0f;
            float hue = 0.5f;        // pick a colour once at spawn, keep it stable

            juce::Rectangle<float> bellBounds() const
            {
                const float r = size * 0.85f;
                return { pos.x - r, pos.y - r, r * 2.0f, r * 2.0f };
            }
        };

        void timerCallback() override
        {
            if (jellies.empty())
                return;

            const float dt = 1.0f / 30.0f;
            auto bounds = getLocalBounds().toFloat();

            for (auto& j : jellies)
            {
                j.phase += dt * j.pulseSpeed;

                j.vel.x += (rng.nextFloat() - 0.5f) * 6.0f * dt;
                j.vel.y += (rng.nextFloat() - 0.5f) * 6.0f * dt - 2.0f * dt;

                const float maxSpeed = 30.0f;
                const float speed = j.vel.getDistanceFromOrigin();
                if (speed > maxSpeed)
                    j.vel *= (maxSpeed / speed);

                j.pos += j.vel * dt;

                const float r = j.size;
                if (j.pos.x < bounds.getX() + r)      { j.pos.x = bounds.getX() + r;      j.vel.x = std::abs (j.vel.x); }
                if (j.pos.x > bounds.getRight() - r)   { j.pos.x = bounds.getRight() - r;  j.vel.x = -std::abs (j.vel.x); }
                if (j.pos.y < bounds.getY() + r)       { j.pos.y = bounds.getY() + r;      j.vel.y = std::abs (j.vel.y); }
                if (j.pos.y > bounds.getBottom() - r)  { j.pos.y = bounds.getBottom() - r; j.vel.y = -std::abs (j.vel.y); }
            }

            repaint();
        }

        void drawJellyfish (juce::Graphics& g, const Jellyfish& j)
        {
            const float pulse = 1.0f + 0.15f * std::sin (j.phase);
            const float s = j.size * pulse;

            const auto glow  = juce::Colour::fromHSV (j.hue, 0.55f, 1.0f, 0.28f);
            const auto body  = juce::Colour::fromHSV (j.hue, 0.45f, 1.0f, 0.55f);
            const auto highlight = juce::Colour::fromHSV (j.hue, 0.2f, 1.0f, 0.7f);

            // Soft outer glow.
            g.setColour (glow);
            g.fillEllipse (j.pos.x - s * 1.3f, j.pos.y - s * 1.1f, s * 2.6f, s * 2.2f);

            // Bell (dome shape, flat-ish underside).
            juce::Path bell;
            bell.startNewSubPath (j.pos.x - s, j.pos.y);
            bell.cubicTo (j.pos.x - s, j.pos.y - s * 1.3f, j.pos.x + s, j.pos.y - s * 1.3f, j.pos.x + s, j.pos.y);
            bell.cubicTo (j.pos.x + s * 0.6f, j.pos.y + s * 0.15f, j.pos.x - s * 0.6f, j.pos.y + s * 0.15f, j.pos.x - s, j.pos.y);
            bell.closeSubPath();
            g.setColour (body);
            g.fillPath (bell);

            g.setColour (highlight);
            g.fillEllipse (j.pos.x - s * 0.45f, j.pos.y - s * 1.05f, s * 0.55f, s * 0.5f);

            const int numTentacles = 5;
            for (int t = 0; t < numTentacles; ++t)
            {
                const float xOff = juce::jmap ((float) t, 0.0f, (float) (numTentacles - 1), -s * 0.75f, s * 0.75f);
                const float startX = j.pos.x + xOff;
                const float startY = j.pos.y + s * 0.1f;
                const float length = s * (1.4f + 0.3f * std::sin (j.phase * 0.5f + t));

                juce::Path tentacle;
                tentacle.startNewSubPath (startX, startY);
                const int segments = 5;
                for (int seg = 1; seg <= segments; ++seg)
                {
                    const float frac = (float) seg / (float) segments;
                    const float wave = std::sin (j.phase * 1.3f + t * 1.7f + frac * 4.0f) * s * 0.18f * frac;
                    tentacle.lineTo (startX + wave, startY + length * frac);
                }
                g.setColour (body.withAlpha (body.getFloatAlpha() * 0.5f));
                g.strokePath (tentacle, juce::PathStrokeType (juce::jmax (1.0f, s * 0.06f)));
            }
        }

        std::vector<Jellyfish> jellies;
        juce::Random rng;
    };
}
