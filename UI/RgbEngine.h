#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "ThemeManager.h"
#include <vector>
#include <cmath>

namespace vlab::ui
{
    enum class RgbMode
    {
        Off = 0,
        RainbowCycle,   // continuous hue rotation through the full spectrum
        GradientCycle,  // continuous cycle through user-defined colour stops
        Pulse,          // smooth sine breathing of brightness around the theme's own accent hue
        Strobe,         // hard on/off flash between accent colour and background
        StrobeRainbow,  // hard flash that also jumps to a new hue each flash
        Chase,          // gradient "chases" itself between accent and glow, comet-style
    };

    inline juce::StringArray rgbModeNames()
    {
        return { "Off", "Rainbow Cycle", "Custom Gradient Cycle", "Pulse / Breathe", "Strobe", "Strobe Rainbow", "Chase" };
    }

    struct RgbSettings
    {
        RgbMode mode          = RgbMode::Off;
        float speedHz         = 0.25f; // cycles per second, 0.02 (glacial) to 5.0 (frantic)
        float saturation      = 0.85f; // 0-1, used by hue-generating modes (Rainbow / Strobe Rainbow)
        float brightness      = 1.0f;  // 0.1-1.5, overall brightness multiplier
        float strobeDuty      = 0.5f;  // 0.05-0.95, fraction of each strobe cycle spent "on"
        bool  applyToBackground = false; // also faintly tint bg/panel/stroke, not just accent

        std::vector<juce::Colour> gradientStops
        {
            juce::Colour (0xffff2e6b), juce::Colour (0xff2ee6ff), juce::Colour (0xff8f2eff)
        }; // used by GradientCycle and Chase; always kept at >= 2 entries
    };

    class RgbEngine : private juce::Timer
    {
    public:
        RgbEngine (LookAndFeel& lnfIn, ThemeManager& themeManagerIn)
            : lnf (lnfIn), themeManager (themeManagerIn) {}

        std::function<void()> onTick; // fires after every applied frame, so callers can repaint

        void setSettings (RgbSettings s)
        {
            if (s.gradientStops.size() < 2)
                s.gradientStops = RgbSettings{}.gradientStops;
            settings = s;
            if (settings.mode == RgbMode::Off)
                stop();
            else
                start();
        }

        const RgbSettings& getSettings() const { return settings; }
        bool isActive() const { return settings.mode != RgbMode::Off; }

        void setTargetHz (int hz)
        {
            targetHz = juce::jlimit (10, 240, hz);
            if (isTimerRunning())
                startTimerHz (targetHz);
        }
        int getTargetHz() const { return targetHz; }

        void start()
        {
            captureBase();
            phase = 0.0;
            lastTicks = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (targetHz);
        }

        void stop()
        {
            stopTimer();
            // Snap straight back to the plain theme, no residual tint.
            if (basePaletteValid)
            {
                setPalette (basePalette);
                lnf.applyPaletteColours();
                if (onTick) onTick();
            }
        }

        void refreshBase() { captureBase(); }

        // ---- persistence: remember RGB settings across restarts ----
        void savePreference (const juce::File& prefFile) const
        {
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("mode", (int) settings.mode);
            obj->setProperty ("speedHz", (double) settings.speedHz);
            obj->setProperty ("saturation", (double) settings.saturation);
            obj->setProperty ("brightness", (double) settings.brightness);
            obj->setProperty ("strobeDuty", (double) settings.strobeDuty);
            obj->setProperty ("applyToBackground", settings.applyToBackground);
            juce::Array<juce::var> stopsArr;
            for (auto& c : settings.gradientStops)
                stopsArr.add ("#" + c.toDisplayString (true).toLowerCase());
            obj->setProperty ("gradientStops", stopsArr);
            prefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        }

        void loadPreference (const juce::File& prefFile)
        {
            if (! prefFile.existsAsFile())
                return;
            auto parsed = juce::JSON::parse (prefFile);
            if (! parsed.isObject())
                return;

            RgbSettings s;
            s.mode = (RgbMode) (int) parsed.getProperty ("mode", 0);
            s.speedHz = (float) (double) parsed.getProperty ("speedHz", (double) s.speedHz);
            s.saturation = (float) (double) parsed.getProperty ("saturation", (double) s.saturation);
            s.brightness = (float) (double) parsed.getProperty ("brightness", (double) s.brightness);
            s.strobeDuty = (float) (double) parsed.getProperty ("strobeDuty", (double) s.strobeDuty);
            s.applyToBackground = (bool) parsed.getProperty ("applyToBackground", s.applyToBackground);

            if (auto* arr = parsed.getProperty ("gradientStops", juce::var()).getArray())
            {
                std::vector<juce::Colour> stops;
                for (auto& v : *arr)
                {
                    auto hex = v.toString();
                    if (hex.startsWithChar ('#')) hex = hex.substring (1);
                    if (hex.length() == 6) hex = "ff" + hex;
                    if (hex.length() == 8 && hex.containsOnly ("0123456789abcdefABCDEF"))
                        stops.push_back (juce::Colour::fromString (hex));
                }
                if (stops.size() >= 2)
                    s.gradientStops = stops;
            }
            setSettings (s);
        }

    private:
        LookAndFeel& lnf;
        ThemeManager& themeManager;
        RgbSettings settings;
        Palette basePalette;
        bool basePaletteValid = false;
        double phase = 0.0; // 0..1, wraps every cycle
        double lastTicks = 0.0;
        int targetHz = 60;
        juce::Random rng { 1234 };
        float strobeSeedHue = -1.0f; // negative = "not yet picked", forces a hue on the first tick

        void captureBase()
        {
            Palette p;
            if (themeManager.getPaletteByName (themeManager.getLastAppliedThemeName(), p))
                basePalette = p;
            else
                basePalette = palette(); // fall back to whatever's currently live
            basePaletteValid = true;
        }

        static juce::Colour scaleBrightness (juce::Colour c, float mult)
        {
            float h, s, v;
            c.getHSB (h, s, v);
            v = juce::jlimit (0.0f, 1.0f, v * juce::jlimit (0.0f, 1.5f, mult));
            return juce::Colour::fromHSV (h, s, v, c.getFloatAlpha());
        }

        // Cycles continuously through `stops` as t sweeps 0..1.
        static juce::Colour sampleGradient (const std::vector<juce::Colour>& stops, float t)
        {
            const int n = (int) stops.size();
            if (n == 0) return juce::Colours::white;
            if (n == 1) return stops[0];
            const float scaled = t * (float) n;
            const float base = std::floor (scaled);
            const int i0 = ((int) base) % n;
            const int i1 = (i0 + 1) % n;
            const float frac = scaled - base;
            return stops[(size_t) i0].interpolatedWith (stops[(size_t) i1], juce::jlimit (0.0f, 1.0f, frac));
        }

        void applyAccent (Palette& p, juce::Colour c, bool dimGlow = false)
        {
            p.accent = c;
            p.accentDim = c.darker (0.35f);
            p.accentGlow = c.withAlpha (dimGlow ? 0.05f : juce::jlimit (0.0f, 1.0f, 0.35f * uiPreset().glowIntensity + 0.1f));
            if (settings.applyToBackground)
                tintBackground (p, c);
        }

        void tintBackground (Palette& p, juce::Colour c) const
        {
            p.panel = basePalette.panel.interpolatedWith (c, 0.08f);
            p.panelAlt = basePalette.panelAlt.interpolatedWith (c, 0.10f);
            p.stroke = basePalette.stroke.interpolatedWith (c, 0.18f);
        }

        void timerCallback() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double dt = juce::jlimit (0.0, 0.25, (now - lastTicks) / 1000.0); // guard vs. big stalls
            lastTicks = now;

            const double wrapDelta = dt * (double) settings.speedHz;
            const double prevPhase = phase;
            phase += wrapDelta;
            phase -= std::floor (phase);
            const bool justWrapped = phase < prevPhase;

            Palette p = basePalette;
            const float t = (float) phase;

            switch (settings.mode)
            {
                case RgbMode::Off:
                    return;

                case RgbMode::RainbowCycle:
                {
                    auto c = juce::Colour::fromHSV (t, settings.saturation, juce::jlimit (0.0f, 1.0f, settings.brightness), 1.0f);
                    applyAccent (p, c);
                    break;
                }
                case RgbMode::GradientCycle:
                {
                    auto c = sampleGradient (settings.gradientStops, t);
                    applyAccent (p, scaleBrightness (c, settings.brightness));
                    break;
                }
                case RgbMode::Pulse:
                {
                    const float breathe = 0.35f + 0.65f * (0.5f + 0.5f * std::sin (t * juce::MathConstants<float>::twoPi));
                    applyAccent (p, scaleBrightness (basePalette.accent, breathe * settings.brightness));
                    break;
                }
                case RgbMode::Strobe:
                {
                    const bool on = t < settings.strobeDuty;
                    auto c = on ? scaleBrightness (basePalette.accent, settings.brightness * 1.3f) : basePalette.bg;
                    applyAccent (p, c, ! on);
                    break;
                }
                case RgbMode::StrobeRainbow:
                {
                    if (justWrapped || strobeSeedHue < 0.0f)
                        strobeSeedHue = rng.nextFloat();
                    const bool on = t < settings.strobeDuty;
                    auto flashColour = juce::Colour::fromHSV (strobeSeedHue, settings.saturation,
                                                               juce::jlimit (0.0f, 1.0f, settings.brightness), 1.0f);
                    auto c = on ? flashColour : basePalette.bg;
                    applyAccent (p, c, ! on);
                    break;
                }
                case RgbMode::Chase:
                {
                    auto cMain = sampleGradient (settings.gradientStops, t);
                    auto cGlow = sampleGradient (settings.gradientStops, std::fmod (t + 0.15f, 1.0f));
                    p.accent = scaleBrightness (cMain, settings.brightness);
                    p.accentDim = p.accent.darker (0.35f);
                    p.accentGlow = cGlow.withAlpha (juce::jlimit (0.0f, 1.0f, 0.45f * uiPreset().glowIntensity + 0.1f));
                    if (settings.applyToBackground)
                        tintBackground (p, cMain);
                    break;
                }
            }

            setPalette (p);
            lnf.applyPaletteColours();
            if (onTick) onTick();
        }
    };
}
