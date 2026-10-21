#pragma once
#include <juce_core/juce_core.h>
#include <map>

namespace vlab::ui
{
    class TranslationManager
    {
    public:
        TranslationManager()
        {
            defaults["boardTab"] = "Board";
            defaults["fxRackTab"] = "FX Rack";
            defaults["settingsTab"] = "Settings";
            defaults["visualizerTab"] = "Visualizer";
            defaults["modsTab"] = "Mods";
            defaults["synthTab"] = "Synth";
            defaults["themeCreatorTab"] = "Themes";
            defaults["ttsTab"] = "TTS";
            defaults["importButton"] = "Import";
            defaults["stopAllSounds"] = "Stop All Sounds";

            // Sidebar / soundboard
            defaults["stopSong"] = "Stop Song";
            defaults["reloadSongs"] = "Reload Songs";

            // Engine
            defaults["refreshDevices"] = "Refresh Devices";
            defaults["startEngine"] = "Start Engine";

            // FX rack
            defaults["fxReset"] = "Reset All FX to Defaults";
            defaults["fxSavePreset"] = "Save Preset...";
            defaults["fxDeletePreset"] = "Delete";

            // Settings
            defaults["setRecordHotkey"] = "Set Record Hotkey";
            defaults["chooseStartupSound"] = "Choose Startup Sound...";
            defaults["loadBackground"] = "Load Background Image/GIF";
            defaults["clearBackground"] = "Clear Background";
            defaults["openRecordingsFolder"] = "Open Recordings Folder";
            defaults["reloadAllMods"] = "Reload All Mods";
            defaults["openModsFolder"] = "Open Mods Folder";

            // Synth
            defaults["synthPlay"] = "Play Song";
            defaults["synthStop"] = "Stop Song / Note";

            // TTS
            defaults["speak"] = "Speak";
            defaults["stopTts"] = "Stop TTS";

            builtins["English"] = {}; // empty override map = always fall back to defaults
        }

        void scanLanguageMods (const juce::File& folder)
        {
            if (! folder.isDirectory())
                return;
            for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.lang.json"))
            {
                auto parsed = juce::JSON::parse (f);
                if (! parsed.isObject())
                    continue;

                auto name = parsed.getProperty ("name", juce::var()).toString();
                if (name.isEmpty())
                    name = f.getFileNameWithoutExtension();

                std::map<std::string, juce::String> strings;
                if (auto* stringsObj = parsed.getProperty ("strings", juce::var()).getDynamicObject())
                {
                    for (auto& prop : stringsObj->getProperties())
                        if (prop.value.isString())
                            strings[prop.name.toString().toStdString()] = prop.value.toString();
                }

                loaded[name.toStdString()] = std::move (strings);
            }
        }

        void reloadLanguageMods (const juce::File& folder)
        {
            loaded.clear();
            scanLanguageMods (folder);
        }

        juce::StringArray getLanguageNames() const
        {
            juce::StringArray names;
            for (auto& kv : builtins) names.add (kv.first);
            for (auto& kv : loaded) names.add (kv.first);
            return names;
        }

        bool applyLanguage (const juce::String& name)
        {
            auto it = builtins.find (name.toStdString());
            if (it != builtins.end())
            {
                active = it->second;
                lastAppliedName = name;
                return true;
            }
            auto it2 = loaded.find (name.toStdString());
            if (it2 != loaded.end())
            {
                active = it2->second;
                lastAppliedName = name;
                return true;
            }
            return false;
        }

        bool isBuiltIn (const juce::String& name) const { return builtins.count (name.toStdString()) != 0; }

        juce::String getLastAppliedLanguageName() const { return lastAppliedName; }

        // looks up the literal English text as a key; unlisted text is returned unchanged
        juce::String text (const juce::String& english) const
        {
            if (active.empty() || english.isEmpty())
                return english;
            auto it = active.find (english.toStdString());
            if (it != active.end() && it->second.isNotEmpty())
                return it->second;
            return english;
        }

        const std::map<std::string, juce::String>& getDefaultKeyStrings() const { return defaults; }

        // Get translated text, fallback to English then key itself
        juce::String t (const juce::String& key) const
        {
            auto it = active.find (key.toStdString());
            if (it != active.end() && it->second.isNotEmpty())
                return it->second;
            auto dit = defaults.find (key.toStdString());
            if (dit != defaults.end())
                return dit->second;
            return key;
        }

        // ---- persistence: remember the chosen language across restarts ----
        void savePreference (const juce::File& prefFile) const
        {
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("language", lastAppliedName);
            prefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        }

        juce::String loadPreference (const juce::File& prefFile) const
        {
            if (! prefFile.existsAsFile())
                return {};
            auto parsed = juce::JSON::parse (prefFile);
            return parsed.getProperty ("language", juce::String()).toString();
        }

    private:
        std::map<std::string, juce::String> defaults;
        std::map<std::string, std::map<std::string, juce::String>> builtins;
        std::map<std::string, std::map<std::string, juce::String>> loaded;
        std::map<std::string, juce::String> active; // empty = English defaults
        juce::String lastAppliedName = "English";
    };
}
