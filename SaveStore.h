#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace vlab
{
    class SaveStore
    {
    public:
        SaveStore()
        {
            stagingDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VLAB_staging");
            stagingDir.createDirectory();

            for (auto& g : groups)
                g.load();
        }

        ~SaveStore()
        {
            stagingDir.deleteRecursively();
        }

        // Get temp file with current content
        juce::File stagingFileFor (const juce::String& key)
        {
            auto file = stagingDir.getChildFile (safeFileName (key) + ".json");
            auto& g = groupFor (key);
            auto section = g.root->getProperty (key);
            if (! section.isVoid())
                file.replaceWithText (juce::JSON::toString (section));
            else
                file.deleteFile();
            return file;
        }

        void commit (const juce::String& key)
        {
            auto file = stagingDir.getChildFile (safeFileName (key) + ".json");
            auto& g = groupFor (key);
            g.root->setProperty (key, file.existsAsFile() ? juce::JSON::parse (file) : juce::var());
            g.save();
        }

    private:
        struct Group
        {
            const char* fileName;
            juce::File targetFile;
            juce::DynamicObject::Ptr root { new juce::DynamicObject() };

            static constexpr const char* kHeaderComment =
                "// DO NOT TOUCH this is a vlab save file, tampering with it can cause TOTAL "
                "vlab annihilation...well not really but its better not to touch this as it "
                "can cause a crash or break the app";

            void load()
            {
                targetFile = juce::File::getCurrentWorkingDirectory().getChildFile (fileName);
                if (! targetFile.existsAsFile())
                    return;

                auto text = targetFile.loadFileAsString();
                while (text.startsWith ("//"))
                {
                    const int nl = text.indexOfChar ('\n');
                    if (nl < 0) { text.clear(); break; }
                    text = text.substring (nl + 1);
                }

                auto parsed = juce::JSON::parse (text);
                if (auto* obj = parsed.getDynamicObject())
                    root = obj;
            }

            void save()
            {
                juce::String text;
                text << kHeaderComment << "\n" << juce::JSON::toString (juce::var (root.get()));
                targetFile.replaceWithText (text);
            }
        };

        std::array<Group, 4> groups {{
            { "vlab_ui_prefs.json" },
            { "vlab_behavior_prefs.json" },
            { "vlab_soundboard.json" },
            { "vlab_content.json" },
        }};

        juce::File stagingDir;

        Group& groupFor (const juce::String& key)
        {
            static const std::pair<const char*, int> table[] = {
                { "theme", 0 }, { "uiPreset", 0 }, { "language", 0 }, { "background", 0 },
                { "splashStyle", 0 }, { "device", 0 },
                { "rgb", 0 }, { "startupSound", 0 },
                { "recordHotkey", 1 }, { "experimental", 1 }, { "discordRpc", 1 }, { "performance", 1 },
                { "soundboardConfig", 2 }, { "deletedRecordings", 2 },
                { "fxPresets", 3 }, { "arpSongs", 3 },
            };
            for (auto& e : table)
                if (key == e.first)
                    return groups[(size_t) e.second];
            return groups[0]; // an unlisted key still gets saved somewhere rather than silently dropped
        }

        static juce::String safeFileName (const juce::String& key)
        {
            return juce::File::createLegalFileName (key);
        }
    };
}
