#pragma once
#include "RealtimeSynth.h"
#include "Song.h"
#include <map>
#include <string>

namespace vlab
{
    class SongSequencer
    {
    public:
        SongSequencer (RealtimeSynth& synthIn, juce::File songsFileIn, juce::File modSongsFolderIn)
            : synth (synthIn), songsFile (std::move (songsFileIn)), modSongsFolder (std::move (modSongsFolderIn))
        {
            reloadSongs();
        }

        void reloadSongs() { songs = loadSongs (songsFile, modSongsFolder); }

        bool playSong (const std::string& name)
        {
            auto it = songs.find (name);
            if (it == songs.end())
                return false;
            currentSong = name;
            synth.loadSong (it->second);
            if (! synth.running.load())
                synth.noteOn();
            return true;
        }

        void stop()
        {
            synth.songActive = false;
            synth.songSteps.clear();
        }

        const std::map<std::string, Song>& getSongs() const { return songs; }
        std::string currentSong;

    private:
        RealtimeSynth& synth;
        juce::File songsFile, modSongsFolder;
        std::map<std::string, Song> songs;
    };
}
