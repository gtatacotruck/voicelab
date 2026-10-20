#pragma once
#include <string>
#include <vector>
#include <map>
#include <juce_core/juce_core.h>

namespace vlab
{
    struct SongStep
    {
        std::string note = "C4";
        float dur = 0.25f;   // duration in beats
        float vel = 1.0f;    // 0..1
        bool  rest = false;
    };

    struct Song
    {
        std::vector<SongStep> steps;
        bool loop = true;
    };

    inline Song makeSong (const std::vector<std::pair<std::string, float>>& notesDur, float vel = 0.8f)
    {
        Song s;
        for (auto& [n, d] : notesDur)
            s.steps.push_back ({ n, d, vel, false });
        return s;
    }

    inline SongStep makeRest (float dur) { return { "C4", dur, 0.0f, true }; }

    inline std::map<std::string, Song> builtinSongs()
    {
        std::map<std::string, Song> m;
        m["Major Triad Up"] = makeSong ({ {"C4",0.25f},{"E4",0.25f},{"G4",0.25f},{"C5",0.25f} });
        m["Minor Triad Up"] = makeSong ({ {"C4",0.25f},{"Eb4",0.25f},{"G4",0.25f},{"C5",0.25f} });
        m["Octave Bounce"]  = makeSong ({ {"C4",0.25f},{"C5",0.25f},{"G4",0.25f},{"C5",0.25f} });
        m["Pentatonic Up"]  = makeSong ({ {"C4",0.2f},{"D4",0.2f},{"E4",0.2f},{"G4",0.2f},{"A4",0.2f} });
        m["Blues Scale"]    = makeSong ({ {"C4",0.2f},{"Eb4",0.2f},{"F4",0.2f},{"Gb4",0.2f},{"G4",0.2f},{"Bb4",0.2f} });
        m["Major Scale"]    = makeSong ({ {"C4",0.25f},{"D4",0.25f},{"E4",0.25f},{"F4",0.25f},
                                            {"G4",0.25f},{"A4",0.25f},{"B4",0.25f},{"C5",0.25f} });
        m["Phrygian Mode"]  = makeSong ({ {"E4",0.25f},{"F4",0.25f},{"G4",0.25f},{"A4",0.25f},
                                            {"B4",0.25f},{"C5",0.25f},{"D5",0.25f},{"E5",0.25f} });
        {
            std::vector<std::pair<std::string, float>> chromatic;
            for (auto* n : { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" })
                chromatic.push_back ({ std::string (n) + "4", 0.125f });
            m["Chromatic Run"] = makeSong (chromatic, 0.7f);
        }
        return m;
    }

    inline std::map<std::string, Song> loadSongs (const juce::File& songsFile, const juce::File& modSongsFolder)
    {
        auto songs = builtinSongs();

        auto parseOne = [] (const juce::var& v) -> Song
        {
            Song s;
            s.loop = v.getProperty ("loop", true);
            if (auto* arr = v.getProperty ("steps", juce::var()).getArray())
            {
                for (auto& stepVar : *arr)
                {
                    SongStep step;
                    step.note = stepVar.getProperty ("note", "C4").toString().toStdString();
                    step.dur  = (float) (double) stepVar.getProperty ("dur", 0.25);
                    step.vel  = (float) (double) stepVar.getProperty ("vel", 1.0);
                    step.rest = (bool) stepVar.getProperty ("rest", false);
                    s.steps.push_back (step);
                }
            }
            return s;
        };

        if (songsFile.existsAsFile())
        {
            auto parsed = juce::JSON::parse (songsFile);
            if (auto* obj = parsed.getDynamicObject())
                for (auto& prop : obj->getProperties())
                    songs[prop.name.toString().toStdString()] = parseOne (prop.value);
        }

        if (modSongsFolder.isDirectory())
        {
            for (auto& f : modSongsFolder.findChildFiles (juce::File::findFiles, false, "*.json"))
            {
                auto parsed = juce::JSON::parse (f);
                if (parsed.isObject())
                    songs[f.getFileNameWithoutExtension().toStdString()] = parseOne (parsed);
            }
        }
        return songs;
    }

    inline void saveSong (const juce::File& songsFile, const std::string& name, const Song& song)
    {
        juce::var root = songsFile.existsAsFile() ? juce::JSON::parse (songsFile) : juce::var (new juce::DynamicObject());
        auto* obj = root.getDynamicObject();
        if (obj == nullptr) { root = juce::var (new juce::DynamicObject()); obj = root.getDynamicObject(); }

        auto songObj = std::make_unique<juce::DynamicObject>();
        songObj->setProperty ("loop", song.loop);
        juce::Array<juce::var> stepsArr;
        for (auto& st : song.steps)
        {
            auto stepObj = std::make_unique<juce::DynamicObject>();
            stepObj->setProperty ("note", juce::String (st.note));
            stepObj->setProperty ("dur", (double) st.dur);
            stepObj->setProperty ("vel", (double) st.vel);
            stepObj->setProperty ("rest", st.rest);
            stepsArr.add (juce::var (stepObj.release()));
        }
        songObj->setProperty ("steps", stepsArr);
        obj->setProperty (juce::String (name), juce::var (songObj.release()));

        songsFile.replaceWithText (juce::JSON::toString (root));
    }
}
