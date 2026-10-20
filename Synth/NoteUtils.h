#pragma once
#include <string>
#include <cmath>
#include <map>
#include <cctype>

namespace vlab
{
    inline int noteToMidi (const std::string& nameIn)
    {
        static const std::map<std::string, int> noteMap {
            { "C", 0 }, { "C#", 1 }, { "Db", 1 }, { "D", 2 }, { "D#", 3 }, { "Eb", 3 },
            { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "Gb", 6 }, { "G", 7 }, { "G#", 8 },
            { "Ab", 8 }, { "A", 9 }, { "A#", 10 }, { "Bb", 10 }, { "B", 11 }
        };

        std::string name = nameIn;
        // trim
        while (! name.empty() && std::isspace ((unsigned char) name.front())) name.erase (name.begin());
        while (! name.empty() && std::isspace ((unsigned char) name.back())) name.pop_back();

        if (name.size() < 2 || ! std::isdigit ((unsigned char) name.back()))
            return 60; // matches the Python's fallback for malformed names

        const int octave = name.back() - '0';
        const std::string pcName = name.substr (0, name.size() - 1);
        auto it = noteMap.find (pcName);
        const int pc = (it != noteMap.end()) ? it->second : 0;
        return (octave + 1) * 12 + pc;
    }

    inline double midiToFreq (int midi)
    {
        return 440.0 * std::pow (2.0, (midi - 69) / 12.0);
    }

    inline double noteNameToFreq (const std::string& name)
    {
        return midiToFreq (noteToMidi (name));
    }

    inline std::string transposeOctave (const std::string& noteIn, int octaves)
    {
        if (noteIn.empty() || ! std::isdigit ((unsigned char) noteIn.back()) || octaves == 0)
            return noteIn;
        const std::string pc = noteIn.substr (0, noteIn.size() - 1);
        const int oct = noteIn.back() - '0';
        const int newOct = std::max (0, std::min (9, oct + octaves));
        return pc + std::to_string (newOct);
    }
}
