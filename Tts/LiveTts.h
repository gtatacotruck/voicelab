#pragma once
#include "../DSP/AudioFileUtils.h"
#include "../Soundboard/SoundboardCore.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

#if JUCE_WINDOWS
 #include <sapi.h>
 #pragma comment(lib, "sapi.lib")
 #pragma comment(lib, "ole32.lib")
#endif

namespace vlab
{
    class LiveTts
    {
    public:
        std::atomic<int> rate { 150 };      // words per minute, matches self.rate
        std::atomic<int> pitch { 50 };      // 0..99, espeak-style scale (50 == neutral), mapped per-platform
        std::atomic<float> volume { 1.0f }; // post-render playback gain (0..~2), applied via ActiveSound.volume
        std::atomic<float> pan { 0.0f };    // -1..1, applied to the isolated TTS bus in Main.cpp's soundboardTtsMix wiring
        juce::String voiceId;               // empty == system default

        explicit LiveTts (SoundboardCore& sbIn) : sb (sbIn)
        {
            formatManager.registerBasicFormats();
            running = true;
            worker = std::thread ([this] { workerLoop(); });
        }

        ~LiveTts()
        {
            stop();
            if (worker.joinable()) worker.join();
        }

        void speak (const juce::String& text)
        {
            if (text.trim().isEmpty())
                return;
            {
                std::lock_guard<std::mutex> lock (queueMutex);
                queue.push_back (text);
            }
            queueCv.notify_one();
        }

        void stop()
        {
            running = false;
            queueCv.notify_all();
        }

        // Returns display names of available voices
        static juce::StringArray getVoices();

    private:
        SoundboardCore& sb;
        juce::AudioFormatManager formatManager;
        std::deque<juce::String> queue;
        std::mutex queueMutex;
        std::condition_variable queueCv;
        std::atomic<bool> running { false };
        std::thread worker;

        void workerLoop()
        {
        #if JUCE_WINDOWS
            const bool comOk = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
        #endif
            while (running.load())
            {
                juce::String text;
                {
                    std::unique_lock<std::mutex> lock (queueMutex);
                    queueCv.wait_for (lock, std::chrono::milliseconds (500),
                                       [this] { return ! queue.empty() || ! running.load(); });
                    if (! running.load()) break;
                    if (queue.empty()) continue;
                    text = queue.front();
                    queue.pop_front();
                }
                renderAndInject (text);
            }
        #if JUCE_WINDOWS
            if (comOk) CoUninitialize();
        #endif
        }

        void renderAndInject (const juce::String& text)
        {
        #if JUCE_MAC
            auto tmpFile = juce::File::createTempFile (".aiff");
        #else
            auto tmpFile = juce::File::createTempFile (".wav");
        #endif

            const bool rendered = renderToFile (text, tmpFile);
            if (rendered && tmpFile.existsAsFile() && tmpFile.getSize() > 0)
            {
                if (auto data = dsp::loadAudioFileAsStereo (tmpFile, formatManager))
                {
                    sb.pushTtsAudio (data, juce::jlimit (0.0f, 2.0f, volume.load()), 1.0f);
                }
            }
            tmpFile.deleteFile();
        }

        bool renderToFile (const juce::String& text, const juce::File& outFile)
        {
        #if JUCE_WINDOWS
            return renderWindowsSapi (text, outFile);
        #elif JUCE_MAC
            return renderMacSay (text, outFile);
        #else
            return renderLinuxEspeak (text, outFile);
        #endif
        }

        // ---- Linux: espeak-ng / espeak CLI ------------------------------------
        bool renderLinuxEspeak (const juce::String& text, const juce::File& outFile)
        {
            for (auto* exe : { "espeak-ng", "espeak" })
            {
                juce::ChildProcess proc;
                juce::StringArray args;
                args.add (exe);
                args.add ("-s"); args.add (juce::String (juce::jlimit (80, 450, rate.load())));
                args.add ("-p"); args.add (juce::String (juce::jlimit (0, 99, pitch.load())));
                if (voiceId.isNotEmpty()) { args.add ("-v"); args.add (voiceId); }
                args.add ("-w"); args.add (outFile.getFullPathName());
                args.add (text);

                if (proc.start (args) && proc.waitForProcessToFinish (10000))
                    if (outFile.existsAsFile() && outFile.getSize() > 0)
                        return true;
            }
            return false;
        }

        // ---- macOS: `say` CLI --------------------------------------------------
        bool renderMacSay (const juce::String& text, const juce::File& outFile)
        {
            juce::ChildProcess proc;
            juce::StringArray args { "say" };
            if (voiceId.isNotEmpty()) { args.add ("-v"); args.add (voiceId); }
            args.add ("-r"); args.add (juce::String (juce::jlimit (80, 450, rate.load())));
            args.add ("-o"); args.add (outFile.getFullPathName());

            const int p = juce::jlimit (0, 99, pitch.load());
            const int pbasHz = 80 + p; // rough neutral-ish range, ~80-179 Hz
            const juce::String spoken = "[[pbas " + juce::String (pbasHz) + "]]" + text;
            args.add (spoken);

            return proc.start (args) && proc.waitForProcessToFinish (10000)
                   && outFile.existsAsFile() && outFile.getSize() > 0;
        }

        // ---- Windows: SAPI5 via ISpVoice ---------------------------------------
        bool renderWindowsSapi (const juce::String& text, const juce::File& outFile);
    };

#if JUCE_WINDOWS
    inline juce::String pickVoiceToken (const juce::String& voiceId, ISpObjectToken** outToken)
    {
        *outToken = nullptr;
        ISpObjectTokenCategory* cat = nullptr;
        if (FAILED (CoCreateInstance (CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                        IID_ISpObjectTokenCategory, (void**) &cat)))
            return {};
        cat->SetId (SPCAT_VOICES, FALSE);

        IEnumSpObjectTokens* enumTokens = nullptr;
        if (FAILED (cat->EnumTokens (nullptr, nullptr, &enumTokens)) || enumTokens == nullptr)
        {
            cat->Release();
            return {};
        }

        juce::String matchedName;
        ISpObjectToken* token = nullptr;
        while (enumTokens->Next (1, &token, nullptr) == S_OK)
        {
            LPWSTR desc = nullptr;
            if (SUCCEEDED (token->GetStringValue (nullptr, &desc)) && desc != nullptr)
            {
                juce::String name (desc);
                CoTaskMemFree (desc);
                if (voiceId.isEmpty())
                {
                    matchedName = name;
                    *outToken = token;
                    break;
                }
                if (name.equalsIgnoreCase (voiceId) || name.containsIgnoreCase (voiceId))
                {
                    matchedName = name;
                    *outToken = token;
                    break;
                }
            }
            token->Release();
            token = nullptr;
        }
        enumTokens->Release();
        cat->Release();
        return matchedName;
    }

    inline bool LiveTts::renderWindowsSapi (const juce::String& text, const juce::File& outFile)
    {
        ISpVoice* voice = nullptr;
        if (FAILED (CoCreateInstance (CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**) &voice))
            || voice == nullptr)
            return false;

        ISpObjectToken* token = nullptr;
        pickVoiceToken (voiceId, &token);
        if (token != nullptr)
        {
            voice->SetVoice (token);
            token->Release();
        }

        WAVEFORMATEX wfx {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = 44100;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (WORD) (wfx.nChannels * wfx.wBitsPerSample / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;

        ISpStream* stream = nullptr;
        if (FAILED (CoCreateInstance (CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, (void**) &stream))
            || stream == nullptr)
        {
            voice->Release();
            return false;
        }

        const auto wideName = outFile.getFullPathName().toWideCharPointer();
        if (FAILED (stream->BindToFile (wideName, SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, SPFEI_ALL_EVENTS)))
        {
            stream->Release();
            voice->Release();
            return false;
        }

        voice->SetOutput (stream, TRUE);

        const int wpm = juce::jlimit (80, 450, rate.load());
        const int sapiRateInt = juce::jlimit (-10, 10, juce::roundToInt ((wpm - 150) / 25.0));
        voice->SetRate ((long) sapiRateInt);

        const int p = juce::jlimit (0, 99, pitch.load());
        const int pitchAbsMiddle = juce::jlimit (-10, 10, juce::roundToInt ((p - 50) / 5.0));
        const juce::String xml = "<pitch absmiddle=\"" + juce::String (pitchAbsMiddle) + "\">"
                                    + text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;")
                                    + "</pitch>";

        const HRESULT hr = voice->Speak (xml.toWideCharPointer(), SPF_IS_XML, nullptr);

        stream->Close();
        stream->Release();
        voice->Release();

        return SUCCEEDED (hr) && outFile.existsAsFile() && outFile.getSize() > 0;
    }

    inline juce::StringArray LiveTts::getVoices()
    {
        juce::StringArray names;
        const bool comOk = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));

        ISpObjectTokenCategory* cat = nullptr;
        if (SUCCEEDED (CoCreateInstance (CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                           IID_ISpObjectTokenCategory, (void**) &cat)))
        {
            cat->SetId (SPCAT_VOICES, FALSE);
            IEnumSpObjectTokens* enumTokens = nullptr;
            if (SUCCEEDED (cat->EnumTokens (nullptr, nullptr, &enumTokens)) && enumTokens != nullptr)
            {
                ISpObjectToken* token = nullptr;
                while (enumTokens->Next (1, &token, nullptr) == S_OK)
                {
                    LPWSTR desc = nullptr;
                    if (SUCCEEDED (token->GetStringValue (nullptr, &desc)) && desc != nullptr)
                    {
                        names.add (juce::String (desc));
                        CoTaskMemFree (desc);
                    }
                    token->Release();
                    token = nullptr;
                }
                enumTokens->Release();
            }
            cat->Release();
        }
        if (comOk) CoUninitialize();
        return names;
    }
#elif JUCE_MAC
    inline juce::StringArray LiveTts::getVoices()
    {
        juce::ChildProcess proc;
        juce::StringArray names;
        if (proc.start (juce::StringArray { "say", "-v", "?" }))
        {
            proc.waitForProcessToFinish (5000);
            auto output = proc.readAllProcessOutput();
            for (auto line : juce::StringArray::fromLines (output))
                if (line.isNotEmpty())
                    names.add (line.upToFirstOccurrenceOf (" ", false, false));
        }
        return names;
    }
#else
    inline juce::StringArray LiveTts::getVoices()
    {
        auto runVoicesQuery = [] (const juce::StringArray& args) -> juce::StringArray
        {
            juce::ChildProcess proc;
            if (proc.start (args))
            {
                proc.waitForProcessToFinish (5000);
                return juce::StringArray::fromLines (proc.readAllProcessOutput());
            }
            return {};
        };

        juce::StringArray names;

        // Base language voices (unchanged behaviour).
        auto baseLines = runVoicesQuery ({ "espeak-ng", "--voices" });
        if (baseLines.isEmpty()) baseLines = runVoicesQuery ({ "espeak", "--voices" });
        for (int i = 1; i < baseLines.size(); ++i) // skip header row
        {
            auto tokens = juce::StringArray::fromTokens (baseLines[i], true);
            if (tokens.size() > 3)
                names.add (tokens[3]);
        }

        auto variantLines = runVoicesQuery ({ "espeak-ng", "--voices=variant" });
        if (variantLines.isEmpty()) variantLines = runVoicesQuery ({ "espeak", "--voices=variant" });
        for (int i = 1; i < variantLines.size(); ++i) // skip header row
        {
            auto tokens = juce::StringArray::fromTokens (variantLines[i], true);
            if (tokens.size() > 3)
                names.add ("en+" + tokens[3]);
        }

        return names;
    }
#endif
}
