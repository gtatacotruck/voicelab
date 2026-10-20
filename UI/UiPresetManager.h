#pragma once
#include "LookAndFeel.h"
#include <juce_core/juce_core.h>
#include <map>

namespace vlab::ui
{
    class UiPresetManager
    {
    public:
        UiPresetManager()
        {
            presets["Default"] = UiPreset{};

            UiPreset blueprint;
            blueprint.cornerRadius = 4.0f;
            blueprint.borderWidth = 1.5f;
            blueprint.useGradients = false;
            blueprint.glowIntensity = 0.3f;
            blueprint.shadowIntensity = 0.0f;
            presets["Blueprint"] = blueprint;

            UiPreset holotech;
            holotech.cornerRadius = 18.0f;
            holotech.borderWidth = 1.0f;
            holotech.glowIntensity = 2.0f;
            holotech.fontScale = 1.05f;
            holotech.accentGradientAngle = 120.0f;
            holotech.accentBorderGlow = true;
            holotech.shadowIntensity = 0.5f;
            presets["Holotech"] = holotech;

            UiPreset vaporDeck;
            vaporDeck.cornerRadius = 20.0f;
            vaporDeck.borderWidth = 1.4f;
            vaporDeck.glowIntensity = 1.6f;
            vaporDeck.pillButtons = true;
            vaporDeck.accentGradientAngle = 60.0f;
            vaporDeck.glassmorphism = true;
            vaporDeck.glassOpacity = 0.45f;
            vaporDeck.shadowIntensity = 0.4f;
            presets["Vapor Deck"] = vaporDeck;

            UiPreset obsidian;
            obsidian.cornerRadius = 8.0f;
            obsidian.borderWidth = 1.0f;
            obsidian.useGradients = false;
            obsidian.glowIntensity = 0.0f;
            obsidian.shadowIntensity = 1.1f;
            presets["Obsidian"] = obsidian;

            UiPreset arcadeCab;
            arcadeCab.cornerRadius = 1.0f;
            arcadeCab.borderWidth = 2.0f;
            arcadeCab.useGradients = false;
            arcadeCab.glowIntensity = 1.0f;
            arcadeCab.scanlines = true;
            arcadeCab.noiseAmount = 0.45f;
            arcadeCab.vignette = 0.65f;
            arcadeCab.shadowIntensity = 0.3f;
            presets["Arcade Cab"] = arcadeCab;

            UiPreset paperConsole;
            paperConsole.cornerRadius = 12.0f;
            paperConsole.borderWidth = 1.0f;
            paperConsole.useGradients = false;
            paperConsole.glowIntensity = 0.0f;
            paperConsole.noiseAmount = 0.2f;
            paperConsole.shadowIntensity = 0.2f;
            presets["Paper Console"] = paperConsole;
        }

        juce::StringArray getPresetNames() const
        {
            juce::StringArray names;
            for (auto& kv : presets) names.add (kv.first);
            return names;
        }

        bool applyPreset (const juce::String& name)
        {
            auto it = presets.find (name.toStdString());
            if (it == presets.end())
                return false;
            setUiPreset (it->second);
            lastAppliedPresetName = name;
            return true;
        }

        juce::String getLastAppliedPresetName() const { return lastAppliedPresetName; }

        void savePreference (const juce::File& prefFile) const
        {
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("uiPreset", lastAppliedPresetName);
            prefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        }

        juce::String loadPreference (const juce::File& prefFile) const
        {
            if (! prefFile.existsAsFile())
                return {};
            auto parsed = juce::JSON::parse (prefFile);
            return parsed.getProperty ("uiPreset", juce::String()).toString();
        }

    private:
        std::map<std::string, UiPreset> presets;
        juce::String lastAppliedPresetName = "Default";
    };
}
