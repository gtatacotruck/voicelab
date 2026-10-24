#pragma once
#include "../DSP/Common.h"
#include "../DSP/AudioFileUtils.h"
#include "ActiveSound.h"
#include "SoundSlot.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <mutex>
#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <functional>

namespace vlab
{
    // boards start with this many slots and grow from there
    constexpr int kMinSlots = 100;
    constexpr int kSlotSanityCap = 100000; // just so a broken config can't ask for millions

    // deque, so pointers to slots stay valid when the board grows (this can sometimes crash)
    using SlotList = std::deque<SoundSlot>;

    struct BoardMeta
    {
        std::string name;
        std::string iconPath; // absolute path to a user-picked picture; empty == default glyph icon
    };

    class SoundboardCore
    {
    public:
        SoundboardCore()
        {
            formatManager.registerBasicFormats();
            ensureAtLeastOneBoard();
        }

        // ---- multi-board management --------------------------------------------
        int getBoardCount() const { return (int) boards.size(); }
        int getActiveBoardIndex() const { return activeBoard; }
        const std::string& getBoardName (int idx) const { return boards[(size_t) idx].name; }
        const std::string& getBoardIconPath (int idx) const { return boards[(size_t) idx].iconPath; }

        void setBoardName (int idx, const std::string& name)
        {
            if (idx < 0 || idx >= (int) boards.size() || name.empty())
                return;
            boards[(size_t) idx].name = name;
        }

        void setBoardIconPath (int idx, const std::string& path)
        {
            if (idx < 0 || idx >= (int) boards.size())
                return;
            boards[(size_t) idx].iconPath = path;
        }

        int addBoard (const std::string& name)
        {
            BoardMeta meta;
            meta.name = name.empty() ? ("Soundboard " + std::to_string (boards.size() + 1)) : name;
            boards.push_back (meta);
            boardData.emplace_back (kMinSlots);
            return (int) boards.size() - 1;
        }

        bool deleteBoard (int idx)
        {
            if ((int) boards.size() <= 1 || idx < 0 || idx >= (int) boards.size())
                return false;

            if (idx == activeBoard)
                setActiveBoard (idx == 0 ? 1 : 0);

            boards.erase (boards.begin() + idx);
            boardData.erase (boardData.begin() + idx);
            if (activeBoard > idx)
                --activeBoard;
            return true;
        }

        void setActiveBoard (int idx)
        {
            if (idx < 0 || idx >= (int) boards.size() || idx == activeBoard)
                return;
            {
                std::lock_guard<std::mutex> lock (mutex);
                boardData[(size_t) activeBoard] = slots; // persist current board's live edits
                activeSounds.clear();
                ttsQueue.clear();
                activeBoard = idx;
                slots = boardData[(size_t) activeBoard];
            }
            loadActiveBoardAudio(); // no-op for any slot already loaded from a previous visit
        }

        void ensureAtLeastOneBoard()
        {
            if (boards.empty())
            {
                BoardMeta meta; meta.name = "Soundboard 1";
                boards.push_back (meta);
                boardData.emplace_back (kMinSlots);
                activeBoard = 0;
            }
            activeBoard = juce::jlimit (0, (int) boards.size() - 1, activeBoard);
            slots = boardData[(size_t) activeBoard];
        }

        void loadActiveBoardAudio()
        {
            for (int i = 0; i < (int) slots.size(); ++i)
            {
                juce::String path;
                {
                    std::lock_guard<std::mutex> lock (mutex);
                    if (slots[(size_t) i].data != nullptr || slots[(size_t) i].path.empty())
                        continue;
                    path = juce::String (slots[(size_t) i].path);
                }

                juce::File f (path);
                if (! f.existsAsFile())
                    continue;
                auto stereo = dsp::loadAudioFileAsStereo (f, formatManager);
                if (stereo == nullptr)
                    continue;

                std::lock_guard<std::mutex> lock (mutex);
                if (i < (int) slots.size())
                    slots[(size_t) i].data = stereo;
            }

            std::lock_guard<std::mutex> lock (mutex);
            if (activeBoard >= 0 && activeBoard < (int) boardData.size())
                boardData[(size_t) activeBoard] = slots;
        }

        // ---- persistence -----------------------------------------------------
        void loadConfig (const juce::File& configFile)
        {
            boards.clear();
            boardData.clear();

            if (! configFile.existsAsFile())
            {
                ensureAtLeastOneBoard();
                return;
            }
            auto parsed = juce::JSON::parse (configFile);
            if (! parsed.isObject())
            {
                ensureAtLeastOneBoard();
                return;
            }

            auto* root = parsed.getDynamicObject();
            if (root != nullptr && root->hasProperty ("boards") && root->getProperty ("boards").isArray())
            {
                for (auto& boardVar : *root->getProperty ("boards").getArray())
                {
                    if (! boardVar.isObject())
                        continue;
                    auto* bObj = boardVar.getDynamicObject();
                    BoardMeta meta;
                    meta.name = bObj->getProperty ("name").toString().toStdString();
                    if (meta.name.empty())
                        meta.name = "Soundboard " + std::to_string (boards.size() + 1);
                    meta.iconPath = bObj->getProperty ("icon").toString().toStdString();

                    SlotList boardSlots (kMinSlots);
                    loadSlotsIntoBoard (bObj->getProperty ("slots"), boardSlots);

                    boards.push_back (meta);
                    boardData.push_back (std::move (boardSlots));
                }
                activeBoard = (int) root->getProperty ("activeBoard");
            }
            else
            {
                BoardMeta meta; meta.name = "Soundboard 1";
                SlotList legacySlots (kMinSlots);
                loadSlotsIntoBoard (parsed, legacySlots);
                boards.push_back (meta);
                boardData.push_back (std::move (legacySlots));
                activeBoard = 0;
            }

            ensureAtLeastOneBoard();
            loadActiveBoardAudio();
        }

        void saveConfig (const juce::File& configFile)
        {
            {
                std::lock_guard<std::mutex> lock (mutex);
                boardData[(size_t) activeBoard] = slots; // sync live edits back before serializing
            }

            juce::Array<juce::var> boardsArr;
            for (size_t bi = 0; bi < boards.size(); ++bi)
            {
                auto boardObj = std::make_unique<juce::DynamicObject>();
                boardObj->setProperty ("name", juce::String (boards[bi].name));
                boardObj->setProperty ("icon", juce::String (boards[bi].iconPath));

                auto slotsObj = std::make_unique<juce::DynamicObject>();
                auto& bs = boardData[bi];
                for (size_t i = 0; i < bs.size(); ++i)
                {
                    auto& s = bs[i];
                    if (s.path.empty() && s.hotkey.empty())
                        continue;
                    auto entry = std::make_unique<juce::DynamicObject>();
                    entry->setProperty ("path", juce::String (s.path));
                    entry->setProperty ("hotkey", juce::String (s.hotkey));
                    entry->setProperty ("trim_start", (double) s.trimStart);
                    entry->setProperty ("trim_end", (double) s.trimEnd);
                    entry->setProperty ("play_mode", playModeToString (s.playMode));
                    juce::Array<juce::var> tagsArr;
                    for (auto& t : s.tags) tagsArr.add (juce::String (t));
                    entry->setProperty ("tags", tagsArr);
                    slotsObj->setProperty (juce::String ((int) i), juce::var (entry.release()));
                }
                boardObj->setProperty ("slots", juce::var (slotsObj.release()));
                boardsArr.add (juce::var (boardObj.release()));
            }

            auto root = std::make_unique<juce::DynamicObject>();
            root->setProperty ("activeBoard", activeBoard);
            root->setProperty ("boards", boardsArr);
            configFile.replaceWithText (juce::JSON::toString (juce::var (root.release())));
        }

        void autoLoadRecordings (const juce::File& recFolder)
        {
            if (! recFolder.isDirectory())
                return;
            for (auto& f : recFolder.findChildFiles (juce::File::findFiles, false, "*.wav;*.mp3"))
            {
                const auto full = f.getFullPathName().toStdString();
                if (ignoredAutoLoadPaths.count (full))
                    continue; // explicitly deleted from a slot before, don't resurrect it

                bool already = std::any_of (slots.begin(), slots.end(),
                                             [&] (const SoundSlot& s) { return s.path == full; });
                if (already) continue;

                int target = -1;
                for (int i = 0; i < (int) slots.size(); ++i)
                    if (slots[(size_t) i].data == nullptr) { target = i; break; }
                if (target < 0)
                    target = (int) slots.size();
                loadFile (target, f.getFullPathName(), false);
            }
        }

        std::set<std::string> ignoredAutoLoadPaths;

        void ignoreAutoLoad (const std::string& path) { ignoredAutoLoadPaths.insert (path); }

        void loadIgnoredAutoLoad (const juce::File& f)
        {
            ignoredAutoLoadPaths.clear();
            if (! f.existsAsFile()) return;
            auto parsed = juce::JSON::parse (f);
            if (auto* arr = parsed.getArray())
                for (auto& v : *arr)
                    ignoredAutoLoadPaths.insert (v.toString().toStdString());
        }

        void saveIgnoredAutoLoad (const juce::File& f) const
        {
            juce::Array<juce::var> arr;
            for (auto& p : ignoredAutoLoadPaths) arr.add (juce::String (p));
            f.replaceWithText (juce::JSON::toString (juce::var (arr)));
        }

        // ---- file loading ------------------------------------------------------

        bool loadFileInto (SlotList& target, int idx, const juce::String& filepath)
        {
            if (idx < 0 || idx >= kSlotSanityCap)
                return false;
            if (idx >= (int) target.size())
                target.resize ((size_t) idx + 1);
            juce::File f (filepath);
            if (! f.existsAsFile())
                return false;

            auto stereo = dsp::loadAudioFileAsStereo (f, formatManager);
            if (stereo == nullptr)
                return false;

            auto& slot = target[(size_t) idx];
            slot.data = stereo;
            slot.path = filepath.toStdString();
            slot.name = f.getFileName().substring (0, 20).toStdString();
            return true;
        }

        bool loadFile (int idx, const juce::String& filepath, bool save = true)
        {
            bool ok;
            {
                std::lock_guard<std::mutex> lock (mutex);
                ok = loadFileInto (slots, idx, filepath);
            }
            if (ok && save && onSaveConfigRequested)
                onSaveConfigRequested();
            return ok;
        }

        void loadSlotsIntoBoard (const juce::var& slotsVar, SlotList& target)
        {
            if (! slotsVar.isObject())
                return;
            auto* obj = slotsVar.getDynamicObject();
            if (obj == nullptr)
                return;

            for (auto& prop : obj->getProperties())
            {
                const int idx = prop.name.toString().getIntValue();
                if (idx < 0 || idx >= kSlotSanityCap || ! prop.value.isObject())
                    continue;
                if (idx >= (int) target.size())
                    target.resize ((size_t) idx + 1);
                auto* v = prop.value.getDynamicObject();
                auto& slot = target[(size_t) idx];

                const juce::String path = v->getProperty ("path").toString();
                if (path.isNotEmpty())
                {
                    slot.path = path.toStdString();
                    slot.name = juce::File (path).getFileName().substring (0, 20).toStdString();
                }

                const juce::String hotkey = v->getProperty ("hotkey").toString();
                if (hotkey.isNotEmpty())
                    slot.hotkey = hotkey.toStdString();

                if (v->hasProperty ("trim_start")) slot.trimStart = (float) (double) v->getProperty ("trim_start");
                if (v->hasProperty ("trim_end"))   slot.trimEnd   = (float) (double) v->getProperty ("trim_end");
                if (v->hasProperty ("play_mode"))  slot.playMode  = playModeFromString (v->getProperty ("play_mode").toString());
                if (v->hasProperty ("tags"))
                {
                    slot.tags.clear();
                    if (auto* arr = v->getProperty ("tags").getArray())
                        for (auto& t : *arr) slot.tags.push_back (t.toString().toStdString());
                }
            }
        }

        void saveRecording (int idx, const juce::File& recFolder)
        {
            std::vector<vlab::AudioBuffer> chunks;
            {
                std::lock_guard<std::mutex> lock (mutex);
                if (recBuffer.empty()) return;
                chunks = recBuffer;
            }
            int total = 0;
            for (auto& c : chunks) total += c.getNumSamples();

            vlab::AudioBuffer full (2, total);
            int pos = 0;
            for (auto& c : chunks) { full.copyFrom (0, pos, c, 0, 0, c.getNumSamples());
                                       full.copyFrom (1, pos, c, 1, 0, c.getNumSamples());
                                       pos += c.getNumSamples(); }

            auto path = recFolder.getChildFile ("rec_" + juce::String (idx) + "_"
                            + juce::String ((juce::int64) juce::Time::currentTimeMillis() / 1000) + ".wav");

            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> stream (path.createOutputStream());
            if (stream != nullptr)
            {
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    wavFormat.createWriterFor (stream.get(), vlab::kSampleRate, 2, 16, {}, 0));
                if (writer != nullptr)
                {
                    stream.release(); // writer now owns it
                    writer->writeFromAudioSampleBuffer (full, 0, total);
                }
            }
            loadFile (idx, path.getFullPathName());
        }

        // ---- playback control ---------------------------------------------------
        // kSampleRate / real device rate. Sounds are stored at kSampleRate, so on a 48k device they'd play fast
        void setDeviceRateRatio (double ratio) { deviceRateRatio.store (juce::jlimit (0.25, 4.0, ratio)); }

        void play (int idx, float speed, float vol, bool loop, bool reverse)
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (idx < 0 || idx >= (int) slots.size()) return;
            auto& s = slots[(size_t) idx];
            if (s.data == nullptr) return;

            std::shared_ptr<const vlab::AudioBuffer> playData = s.data;
            if (s.trimStart > 0.0f || s.trimEnd < 1.0f)
            {
                const int n = s.data->getNumSamples();
                const int start = (int) (s.trimStart * n);
                const int end   = std::max (1, (int) (s.trimEnd * n));
                auto trimmed = std::make_shared<vlab::AudioBuffer> (2, end - start);
                trimmed->copyFrom (0, 0, *s.data, 0, start, end - start);
                trimmed->copyFrom (1, 0, *s.data, 1, start, end - start);
                playData = trimmed;
            }

            if (s.playMode == PlayMode::StopOthers)
                activeSounds.clear(); // solo trigger, stop everything else (and any other instance of this one)
            else if (s.playMode == PlayMode::Retrigger)
                activeSounds.erase (std::remove_if (activeSounds.begin(), activeSounds.end(),
                    [idx] (const ActiveSound& a) { return a.slotIdx == idx; }), activeSounds.end());

            activeSounds.emplace_back (playData, vol, speed, loop, reverse, idx);
        }

        void stopSlot (int idx)
        {
            std::lock_guard<std::mutex> lock (mutex);
            activeSounds.erase (std::remove_if (activeSounds.begin(), activeSounds.end(),
                [idx] (const ActiveSound& s) { return s.slotIdx == idx; }), activeSounds.end());
        }

        void seekSlot (int idx, float fraction)
        {
            std::lock_guard<std::mutex> lock (mutex);
            fraction = juce::jlimit (0.0f, 1.0f, fraction);
            for (auto& s : activeSounds)
            {
                if (s.slotIdx != idx || s.data == nullptr)
                    continue;
                const int dataLen = s.data->getNumSamples();
                if (dataLen <= 0)
                    continue;
                const int target = juce::jlimit (0, dataLen - 1, (int) (fraction * dataLen));
                s.pos = target;
                s.finished = false; // seeking un-finishes a sound that had just ended
            }
        }

        void stopAll()
        {
            std::lock_guard<std::mutex> lock (mutex);
            activeSounds.clear();
            ttsQueue.clear();
        }

        void pushTtsAudio (std::shared_ptr<const vlab::AudioBuffer> data, float volume = 1.0f, float speed = 1.0f)
        {
            std::lock_guard<std::mutex> lock (mutex);
            ttsQueue.emplace_back (std::move (data), volume, speed, false, false);
        }

        void clearTtsQueue()
        {
            std::lock_guard<std::mutex> lock (mutex);
            ttsQueue.clear();
        }

        std::atomic<bool> isRecording { false };

        void startRecording (int targetSlot)
        {
            std::lock_guard<std::mutex> lock (mutex);
            isRecording = true;
            recTarget = targetSlot;
            recBuffer.clear();
        }

        void stopRecording (const juce::File& recFolder)
        {
            isRecording = false;
            if (recTarget >= 0)
                saveRecording (recTarget, recFolder);
        }

        void feedRecordingInput (const vlab::AudioBuffer& in, int numSamples)
        {
            if (! isRecording) return;
            vlab::AudioBuffer copy (2, numSamples);
            copy.copyFrom (0, 0, in, 0, 0, numSamples);
            copy.copyFrom (1, 0, in, 1, 0, numSamples);
            std::lock_guard<std::mutex> lock (mutex);
            recBuffer.push_back (std::move (copy));
        }

        bool isTtsSpeaking()
        {
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& s : ttsQueue)
                if (! s.finished)
                    return true;
            return false;
        }

        // ---- now playing (exposed to mods through the app command API) ----
        struct NowPlayingInfo
        {
            int slotIdx = -1;
            juce::String name;
            double elapsedSeconds = 0.0;
            double totalSeconds = 0.0;
            bool loop = false;
        };

        bool getNowPlaying (NowPlayingInfo& info)
        {
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& s : activeSounds)
            {
                if (s.slotIdx < 0 || s.slotIdx >= (int) slots.size() || s.data == nullptr || s.finished)
                    continue;
                const int n = s.data->getNumSamples();
                if (n <= 0)
                    continue;

                const double sr = (double) vlab::kSampleRate;
                const double speed = s.speed > 0.0f ? (double) s.speed : 1.0;
                const double samplesPlayed = s.reverse ? (double) ((n - 1) - s.pos) : (double) s.pos;

                info.slotIdx = s.slotIdx;
                info.name = juce::String (slots[(size_t) s.slotIdx].name);
                info.totalSeconds = n / sr / speed;
                info.elapsedSeconds = juce::jlimit (0.0, info.totalSeconds, samplesPlayed / sr / speed);
                info.loop = s.loop;
                return true;
            }
            return false;
        }

        void getAudioMix (vlab::AudioBuffer& out, int frameSize, bool includeSb, bool includeTts)
        {
            out.clear();
            std::lock_guard<std::mutex> lock (mutex);

            activeSounds.erase (std::remove_if (activeSounds.begin(), activeSounds.end(),
                [] (const ActiveSound& s) { return s.finished; }), activeSounds.end());
            ttsQueue.erase (std::remove_if (ttsQueue.begin(), ttsQueue.end(),
                [] (const ActiveSound& s) { return s.finished; }), ttsQueue.end());

            std::vector<ActiveSound*> targets;
            if (includeSb)  for (auto& s : activeSounds) targets.push_back (&s);
            if (includeTts) for (auto& s : ttsQueue)      targets.push_back (&s);

            for (auto* sp : targets)
            {
                auto& s = *sp;
                if (s.data == nullptr) continue;
                const int dataLen = s.data->getNumSamples();
                const int needed = (int) std::lround (frameSize * (double) s.speed * deviceRateRatio.load());
                if (needed <= 0) continue;

                vlab::AudioBuffer chunk (2, needed);
                chunk.clear();

                if (! s.reverse)
                {
                    const int rem = dataLen - s.pos;
                    if (rem >= needed)
                    {
                        chunk.copyFrom (0, 0, *s.data, 0, s.pos, needed);
                        chunk.copyFrom (1, 0, *s.data, 1, s.pos, needed);
                        s.pos += needed;
                    }
                    else
                    {
                        if (rem > 0) { chunk.copyFrom (0, 0, *s.data, 0, s.pos, rem);
                                        chunk.copyFrom (1, 0, *s.data, 1, s.pos, rem); }
                        if (s.loop)
                        {
                            const int fade = std::min (256, rem);
                            applyFadeOut (chunk, 0, fade);
                            const int lcLen = std::min (needed - rem, dataLen);
                            vlab::AudioBuffer lc (2, lcLen);
                            lc.copyFrom (0, 0, *s.data, 0, 0, lcLen);
                            lc.copyFrom (1, 0, *s.data, 1, 0, lcLen);
                            applyFadeIn (lc, 0, std::min (fade, lcLen));
                            for (int ch = 0; ch < 2; ++ch)
                                chunk.addFrom (ch, rem, lc, ch, 0, std::min (lcLen, needed - rem));
                            s.pos = needed - rem;
                        }
                        else
                        {
                            s.finished = true;
                        }
                    }
                }
                else
                {
                    const int start = s.pos - needed;
                    if (start >= 0)
                    {
                        copyReversed (chunk, 0, *s.data, start, needed);
                        s.pos -= needed;
                    }
                    else
                    {
                        const int avail = s.pos;
                        if (avail > 0) copyReversed (chunk, 0, *s.data, 0, avail);
                        if (s.loop)
                        {
                            s.pos = dataLen;
                            const int extra = needed - avail;
                            const int srcStart = std::max (0, s.pos - extra);
                            copyReversed (chunk, avail, *s.data, srcStart, std::min (extra, dataLen));
                            s.pos -= extra;
                        }
                        else
                        {
                            s.finished = true;
                        }
                    }
                }

                vlab::AudioBuffer* resized = &chunk;
                vlab::AudioBuffer resampledChunk;
                if (needed != frameSize && needed > 0)
                {
                    resampledChunk = resampleToLength (chunk, frameSize);
                    resized = &resampledChunk;
                }

                const float speedComp = (s.speed < 1.0f) ? std::min (3.0f, 1.0f / std::max (0.01f, s.speed)) : 1.0f;

                bool finite = true;
                for (int ch = 0; ch < 2 && finite; ++ch)
                {
                    auto* d = resized->getReadPointer (ch);
                    for (int i = 0; i < resized->getNumSamples(); ++i)
                        if (! std::isfinite (d[i])) { finite = false; break; }
                }
                if (finite)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        out.addFrom (ch, 0, *resized, ch, 0, juce::jmin (frameSize, resized->getNumSamples()),
                                     s.volume * speedComp);
                }

                if (s.slotIdx >= 0 && s.slotIdx < (int) slots.size() && slots[(size_t) s.slotIdx].data != nullptr)
                    slots[(size_t) s.slotIdx].progress = 100.0f * s.pos / (float) dataLen;
            }
        }

        SlotList& getSlots() { return slots; }

        int getSlotCount() const { return (int) slots.size(); }

        struct HotkeyEntry { int board; int slot; std::string slotName; std::string hotkey; };

        std::vector<HotkeyEntry> collectHotkeys()
        {
            std::lock_guard<std::mutex> lock (mutex);
            std::vector<HotkeyEntry> out;
            for (int b = 0; b < (int) boardData.size(); ++b)
            {
                const SlotList& list = (b == activeBoard) ? slots : boardData[(size_t) b];
                for (int i = 0; i < (int) list.size(); ++i)
                    if (! list[(size_t) i].hotkey.empty())
                        out.push_back ({ b, i, list[(size_t) i].name, list[(size_t) i].hotkey });
            }
            return out;
        }

        void clearHotkey (int board, int slot)
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (board < 0 || board >= (int) boardData.size())
                return;
            SlotList& list = (board == activeBoard) ? slots : boardData[(size_t) board];
            if (slot >= 0 && slot < (int) list.size())
                list[(size_t) slot].hotkey.clear();
        }

        int clearAllHotkeys()
        {
            std::lock_guard<std::mutex> lock (mutex);
            int n = 0;
            for (auto& s : slots)     if (! s.hotkey.empty()) { s.hotkey.clear(); ++n; }
            for (size_t b = 0; b < boardData.size(); ++b)
                if ((int) b != activeBoard)
                    for (auto& s : boardData[b]) if (! s.hotkey.empty()) { s.hotkey.clear(); ++n; }
            return n;
        }

        void ensureSlotCount (int n)
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (n > (int) slots.size() && n <= kSlotSanityCap)
                slots.resize ((size_t) n);
        }

        // keep a few empty slots past the last used one
        void padSlots()
        {
            std::lock_guard<std::mutex> lock (mutex);
            int last = -1;
            for (int i = (int) slots.size() - 1; i >= 0; --i)
                if (! slots[(size_t) i].path.empty() || slots[(size_t) i].data != nullptr) { last = i; break; }
            const int want = std::max (kMinSlots, ((last + 1 + 10 + 9) / 10) * 10);
            if (want > (int) slots.size() && want <= kSlotSanityCap)
                slots.resize ((size_t) want);
        }
        std::mutex& getMutex() { return mutex; }

        std::function<void (int, const std::string&)> onHotkeyRegistered;
        std::function<void()> onSaveConfigRequested;

    private:
        SlotList slots; // the currently-active board's slots, what play()/loadFile()/etc all operate on

        std::vector<BoardMeta> boards;                    // one entry per board, parallel to boardData
        std::vector<SlotList> boardData;    // per-board slot storage; boardData[activeBoard] is stale until synced (see setActiveBoard/saveConfig)
        int activeBoard = 0;
        std::vector<ActiveSound> activeSounds;
        std::vector<ActiveSound> ttsQueue;
        std::mutex mutex;
        std::atomic<double> deviceRateRatio { 1.0 };
        juce::AudioFormatManager formatManager;

        std::vector<vlab::AudioBuffer> recBuffer;
        int recTarget = -1;

        static void applyFadeOut (vlab::AudioBuffer& buf, int startSample, int length)
        {
            if (length <= 0) return;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                    buf.setSample (ch, startSample + i,
                        buf.getSample (ch, startSample + i) * (1.0f - (float) i / (float) juce::jmax (1, length - 1)));
        }
        static void applyFadeIn (vlab::AudioBuffer& buf, int startSample, int length)
        {
            if (length <= 0) return;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                    buf.setSample (ch, startSample + i,
                        buf.getSample (ch, startSample + i) * ((float) i / (float) juce::jmax (1, length - 1)));
        }

        static void copyReversed (vlab::AudioBuffer& dest, int destOffset,
                                   const vlab::AudioBuffer& src, int srcStart, int length)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                    dest.setSample (ch, destOffset + i, src.getSample (ch, srcStart + length - 1 - i));
        }

        static vlab::AudioBuffer resampleToLength (const vlab::AudioBuffer& src, int outLen)
        {
            const int inLen = src.getNumSamples();
            vlab::AudioBuffer out (2, outLen);
            if (inLen <= 1)
            {
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, 0, src, ch, 0, juce::jmin (inLen, outLen));
                return out;
            }
            for (int i = 0; i < outLen; ++i)
            {
                // linspace(0, len(c), frame_size): step = len(c) / (frame_size - 1)
                const double xPos = (outLen > 1) ? (double) i * inLen / (double) (outLen - 1) : 0.0;
                const double xc = juce::jlimit (0.0, (double) inLen, xPos);
                const int i0 = juce::jlimit (0, inLen - 1, (int) xc);
                const int i1 = juce::jlimit (0, inLen - 1, i0 + 1);
                const double frac = xc - i0;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float a = src.getSample (ch, i0), b = src.getSample (ch, i1);
                    out.setSample (ch, i, (float) (a * (1.0 - frac) + b * frac));
                }
            }
            return out;
        }

    };
}
