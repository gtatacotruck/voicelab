
#ifndef JUCE_MODAL_LOOPS_PERMITTED
 #define JUCE_MODAL_LOOPS_PERMITTED 1
#endif

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <random>
#include <thread> // background mod-scanning thread, see MainComponent::loadModsInBackground() (v1.7.1)
#include "AudioEngine.h"
#include "DSP/AudioFileUtils.h"
#include "DSP/FxPresets.h"
#include "Soundboard/SoundboardCore.h"
#include "Synth/RealtimeSynth.h"
#include "Synth/SongSequencer.h"
#include "Synth/Song.h"
#include "Tts/LiveTts.h"
#include "UI/LookAndFeel.h"
#include "UI/SoundSlotCard.h"
#include "UI/MonitorOutput.h"
#include "Discord/DiscordRichPresence.h"
#include "UI/FxSection.h"
#include "UI/SplashScreen.h"
#include "UI/WaveformDisplay.h"
#include "UI/VirtualHeadWidget.h"
#include "UI/SpectrumAnalyzer.h"
#include "UI/Goniometer.h"
#include "UI/VuMeterDisplay.h"
#include "UI/RadialSpectrum.h"
#include "UI/ThemeManager.h"
#include "UI/UiPresetManager.h"
#include "SaveStore.h"
#include "UI/ThemeCreatorPanel.h"
#include "UI/RgbEngine.h"
#include "UI/RgbControlPanel.h"
#include "UI/BackgroundLayer.h"
#include "UI/SystemAudioCapture.h"
#include "UI/TabEchoPanel.h"
#include "UI/GlobalHotkeys.h"
#include "UI/SmoothViewport.h"
#include "UI/BoardSelectorBar.h"
#include "UI/AppModCanvasComponent.h"
#include "Mods/ModHost.h"
#include "Mods/VisualizerModHost.h"
#include "Mods/AppModHost.h"
#include "UI/TranslationManager.h"
#include "Update/UpdateChecker.h"

static constexpr const char* kAppVersionString = "1.9.5";
#include "UI/JellyfishOverlay.h"
#include "UI/ModderView.h"

using namespace vlab::ui;

// Board grid now shows the full soundboard (SOUND_LIMIT = 100 slots).
static constexpr int kVisibleSlots = vlab::kSoundLimit;

class MainComponent : public juce::AudioAppComponent
{
public:
    MainComponent (std::function<void (const juce::String&, int, int)> onLoadProgress = {})
        : reportLoadProgress (std::move (onLoadProgress))
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true); // needed for in-app sound/record hotkeys (see keyPressed() below)

        engine.prepare (vlab::kSampleRate, 512);

        recFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("saved_recordings");
        recFolder.createDirectory();
        configFile = saveStore.stagingFileFor ("soundboardConfig");
        soundboard.onSaveConfigRequested = [this] { soundboard.saveConfig (configFile); saveStore.commit ("soundboardConfig"); };
        soundboard.loadConfig (configFile);
        deletedRecordingsFile = saveStore.stagingFileFor ("deletedRecordings");
        soundboard.loadIgnoredAutoLoad (deletedRecordingsFile);
        soundboard.autoLoadRecordings (recFolder);

        engine.soundboardMix = [this] (vlab::AudioBuffer& out, int n) { soundboard.getAudioMix (out, n, true, false); };
        engine.soundboardTtsMix = [this] (vlab::AudioBuffer& out, int n)
        {
            soundboard.getAudioMix (out, n, false, true);
            vlab::dsp::applyPan (out, tts.pan.load(), n);
        };
        engine.synthRunning  = [this] { return synth.running.load(); };
        engine.synthProcess  = [this] (vlab::AudioBuffer& out, int n) { synth.process (out, n, synthParams); };
        engine.modProcess    = [this] (vlab::AudioBuffer& out, int n) { modHost.process (out, n); };

        modsFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("dll");
        modsFolder.createDirectory();

        // Load songs eagerly for dropdown population
        arpSongsFile = saveStore.stagingFileFor ("arpSongs");
        modSongsFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("songs");
        modSongsFolder.createDirectory();
        sequencer = std::make_unique<vlab::SongSequencer> (synth, arpSongsFile, modSongsFolder);

        visualizersFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("visualizers");
        visualizersFolder.createDirectory(); // scanned in the background, see loadModsInBackground()

        appModsFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("appmods");
        appModsFolder.createDirectory();

        appModHost.getAppVersion    = [] { return juce::String (kAppVersionString); };
        appModHost.getActiveTabName = [this] { return tabToName (currentTab); };
        appModHost.checkForUpdates  = [this]
        {
            updateChecker.checkAsync (kAppVersionString, [this] (vlab::UpdateInfo info) { showUpdateDialog (info); });
        };
        appModHost.switchTab = [this] (juce::String name)
        {
            Tab t;
            if (tabFromName (name, t)) showTab (t);
        };
        appModHost.showMessage = [] (juce::String msg)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "vlab", msg);
        };
        appModHost.quitApp = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        appModHost.runCommand = [this] (juce::String name, juce::String argJson) -> juce::String
        {
            if (name == "get_now_playing")
            {
                vlab::SoundboardCore::NowPlayingInfo info;
                if (! soundboard.getNowPlaying (info))
                    return "{}";
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", info.name);
                o->setProperty ("elapsedSeconds", info.elapsedSeconds);
                o->setProperty ("totalSeconds", info.totalSeconds);
                o->setProperty ("loop", info.loop);
                return juce::JSON::toString (juce::var (o), true);
            }
            if (name == "list_boards")
            {
                juce::Array<juce::var> arr;
                for (int i = 0; i < soundboard.getBoardCount(); ++i)
                    arr.add (juce::String (soundboard.getBoardName (i)));
                return juce::JSON::toString (juce::var (arr), true);
            }
            if (name == "switch_board")
            {
                for (int i = 0; i < soundboard.getBoardCount(); ++i)
                    if (juce::String (soundboard.getBoardName (i)) == argJson) { switchBoard (i); return "true"; }
                return "false";
            }
            if (name == "get_discord_rpc_enabled")
                return discordRpcEnabled ? "true" : "false";
            if (name == "set_discord_rpc_enabled")
            {
                const bool v = argJson.trim().equalsIgnoreCase ("true");
                discordRpcEnabled = v;
                discordRPC.setEnabled (v);
                saveDiscordRpcPreference();
                return "true";
            }
            if (name == "list_soundboard_slots")
            {
                juce::Array<juce::var> arr;
                auto& slots = soundboard.getSlots();
                for (int i = 0; i < (int) slots.size(); ++i)
                {
                    if (slots[(size_t) i].path.empty()) continue;
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("index", i);
                    o->setProperty ("name", juce::String (slots[(size_t) i].name));
                    o->setProperty ("hasHotkey", ! slots[(size_t) i].hotkey.empty());
                    arr.add (juce::var (o));
                }
                return juce::JSON::toString (juce::var (arr), true);
            }
            if (name == "play_soundboard_slot")
            {
                const int idx = argJson.trim().getIntValue();
                auto& slots = soundboard.getSlots();
                if (idx < 0 || idx >= (int) slots.size() || slots[(size_t) idx].path.empty())
                    return "false";
                soundboard.play (idx, sbDefaultSpeed, sbDefaultVolume, sbDefaultLoop, sbDefaultReverse);
                return "true";
            }
            if (name == "list_glitch_effects")
            {
                juce::Array<juce::var> arr;
                for (auto& e : glitchEffectRefs()) arr.add (juce::String (e.name));
                return juce::JSON::toString (juce::var (arr), true);
            }
            if (name == "get_glitch_effect_enabled")
            {
                for (auto& e : glitchEffectRefs())
                    if (argJson.trim() == e.name)
                        return e.enabled->load() ? "true" : "false";
                return "false";
            }
            if (name == "set_glitch_effect_enabled")
            {
                auto parsed = juce::JSON::parse (argJson);
                const auto effectName = parsed.getProperty ("name", "").toString();
                for (auto& e : glitchEffectRefs())
                    if (effectName == e.name)
                    {
                        *e.enabled = (bool) parsed.getProperty ("enabled", false);
                        if (refreshFxSlidersFromParams) refreshFxSlidersFromParams();
                        return "true";
                    }
                return "false";
            }
            if (name == "set_glitch_effect_mix")
            {
                auto parsed = juce::JSON::parse (argJson);
                const auto effectName = parsed.getProperty ("name", "").toString();
                for (auto& e : glitchEffectRefs())
                    if (effectName == e.name)
                    {
                        *e.mix = juce::jlimit (0.0f, 1.0f, (float) (double) parsed.getProperty ("mix", 0.0));
                        if (refreshFxSlidersFromParams) refreshFxSlidersFromParams();
                        return "true";
                    }
                return "false";
            }
            if (name == "get_monitor_volume")
                return juce::String (monitorOut.getVolume(), 3);
            if (name == "set_monitor_volume")
            {
                const float v = juce::jlimit (0.0f, 1.5f, argJson.trim().getFloatValue());
                monitorOut.setVolume (v);
                monitorVolumeSlider.setValue (v, juce::dontSendNotification);
                return "true";
            }
            if (name == "hold_synth_tone")
            {
                auto parsed = juce::JSON::parse (argJson);
                const bool on = (bool) parsed.getProperty ("on", false);
                if (on)
                {
                    const float freq = juce::jlimit (20.0f, 5000.0f, (float) (double) parsed.getProperty ("freq", 440.0));
                    synthParams.freq = freq;
                    synth.noteOn();
                    toneFreqSlider.setValue (freq, juce::dontSendNotification);
                    holdToneToggle.setToggleState (true, juce::dontSendNotification);
                }
                else
                {
                    synth.noteOff();
                    holdToneToggle.setToggleState (false, juce::dontSendNotification);
                }
                return "true";
            }
            if (name == "reset_synth_patch")
            {
                resetSynthPatch();
                return "true";
            }
            return juce::String(); // unrecognized command name
        };
        appModHost.onActionsChanged = [this] { rebuildAppModsUi(); };
        appModHost.reloadAllModsFn = [this] { reloadAllMods(); }; // wires host->reloadAllMods() from VLabAppModAPI.h

        themesFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("themes");
        themesFolder.createDirectory();
        lookAndFeel.setThemesFolder (themesFolder); // where a theme's relative cursorImage path resolves from
        themeManager.scanThemeMods (themesFolder);

        themePrefFile = saveStore.stagingFileFor ("theme");
        {
            const auto savedTheme = themeManager.loadPreference (themePrefFile);
            if (savedTheme.isNotEmpty())
                themeManager.applyTheme (savedTheme);
        }

        uiPresetPrefFile = saveStore.stagingFileFor ("uiPreset");
        {
            const auto savedPreset = uiPresetManager.loadPreference (uiPresetPrefFile);
            if (savedPreset.isNotEmpty())
                uiPresetManager.applyPreset (savedPreset);
            else
                uiPresetManager.applyPreset ("Default");
        }

        splashStylePrefFile = saveStore.stagingFileFor ("splashStyle");
        if (splashStylePrefFile.existsAsFile())
        {
            auto parsed = juce::JSON::parse (splashStylePrefFile);
            if (parsed.isObject())
                splashStyleIndex = juce::jlimit (0, 9, (int) parsed.getProperty ("style", 0));
        }

        discordRpcPrefFile = saveStore.stagingFileFor ("discordRpc");
        loadDiscordRpcPreference();

        modernUiPrefFile = saveStore.stagingFileFor ("modernUi");
        if (modernUiPrefFile.existsAsFile())
        {
            auto parsed = juce::JSON::parse (modernUiPrefFile);
            if (parsed.isObject())
                modernUi = (bool) parsed.getProperty ("enabled", true);
        }
        FxSection::compactMode = modernUi;

        languagesFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("mods").getChildFile ("languages");
        languagesFolder.createDirectory();
        translationManager.scanLanguageMods (languagesFolder);

        languagePrefFile = saveStore.stagingFileFor ("language");
        {
            const auto savedLanguage = translationManager.loadPreference (languagePrefFile);
            translationManager.applyLanguage (savedLanguage.isNotEmpty() ? savedLanguage : "English");
        }

        addAndMakeVisible (backgroundLayer);
        backgroundLayer.setOpacity (0.5f);
        backgroundPrefFile = saveStore.stagingFileFor ("background");
        loadBackgroundPreference();

        rgbPrefFile = saveStore.stagingFileFor ("rgb");
        rgbEngine.onTick = [this]
        {
            for (auto* c : { (juce::Component*) &boardContent, &fxRackContent, &settingsContent,
                              &visualizerContent, &synthContent, &modsContent, &themeCreatorContent })
                c->repaint();
            repaint();
        };
        rgbEngine.loadPreference (rgbPrefFile); // starts the animation immediately if it was on last run

        recordHotkeyPrefFile = saveStore.stagingFileFor ("recordHotkey");
        loadRecordHotkeyPreference();

        globalHotkeys.onTriggered = [this] (const juce::String& d) { triggerHotkeyByDescription (d); };
        refreshGlobalHotkeyWatchList();

        experimentalPrefFile = saveStore.stagingFileFor ("experimental");
        loadExperimentalPreference(); // does NOT apply effects yet, toggles/pointers aren't built until buildSettingsTab()

        performancePrefFile = saveStore.stagingFileFor ("performance");
        loadPerformancePreference();

        devicePrefFile = saveStore.stagingFileFor ("device");
        loadDevicePreference();

        fxPresetsFile = saveStore.stagingFileFor ("fxPresets");

        startupSoundPrefFile = saveStore.stagingFileFor ("startupSound");
        loadStartupSoundPreference();
        loadStartupJingle();

        buildSidebar();
        buildTopBar();
        buildBoardTab();
        buildFxRackTab();
        buildSettingsTab();
        applyAllExperimental(); // now that the Experimental toggles exist, reflect + apply the loaded preference
        applyAllPerformance();
        buildVisualizerTab();
        statsPanel.engine = &engine;
        buildSynthTab();
        buildModsTab();

        appModHost.scan (appModsFolder,
            [this] (int done, int total) { reportProgress ("Loading app mods", done, total); });

        buildThemeCreatorTab();
        buildTtsTab();

        applyTranslations(); // every translatable button now exists; push the saved/default language onto them

        engine.browserAudioMix = [this] (vlab::AudioBuffer& out, int n) { systemAudioCapture.pull (out, n); };

        tabEchoPanel_ = std::make_unique<vlab::ui::TabEchoPanel> (engine.params, systemAudioCapture);
        addAndMakeVisible (*tabEchoPanel_);

        addAndMakeVisible (boardViewport);
        addAndMakeVisible (fxRackViewport);
        addAndMakeVisible (settingsViewport);
        addAndMakeVisible (visualizerViewport);
        addAndMakeVisible (modsViewport);
        addAndMakeVisible (synthViewport);
        addAndMakeVisible (themeCreatorViewport);
        addAndMakeVisible (ttsViewport);
        addChildComponent (appModFullViewHost); // hidden until an App Mod full-view canvas is selected
        boardViewport.setViewedComponent (&boardContent, false);
        fxRackViewport.setViewedComponent (&fxRackContent, false);
        settingsViewport.setViewedComponent (&settingsContent, false);
        visualizerViewport.setViewedComponent (&visualizerContent, false);
        synthViewport.setViewedComponent (&synthContent, false);
        modsViewport.setViewedComponent (&modsContent, false);
        themeCreatorViewport.setViewedComponent (&themeCreatorContent, false);
        ttsViewport.setViewedComponent (&ttsContent, false);
        showTab (Tab::Board);

        setSize (1280, 860);
        setAudioChannels (2, 2);
        applyDeviceSelection(); // route to whatever's actually selected, not JUCE's default device
        running = true;
        startButton.setButtonText ("Stop Engine");
        startButton.getProperties().set ("primary", false);
        startButton.getProperties().set ("danger", true);
        if (startupSoundEnabled && startupJingleBuffer != nullptr)
        {
            startupJinglePos = 0;
            startupJingleActive = true;
        }
        startTimerHz (20);
        grabKeyboardFocus(); // so hotkeys work right away, before the user clicks anything

        discordRPC.setEnabled (discordRpcEnabled);
        discordRPC.start();

        addAndMakeVisible (jellyfishOverlay);
        jellyfishOverlay.toFront (false);
    }

    ~MainComponent() override
    {
        shutdownAudio();
        monitorOut.close();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (pendingBind != BindTarget::None)
        {
            if (key.getKeyCode() == juce::KeyPress::escapeKey)
            {
                recorderStatusLabel.setText ("Bind cancelled.", juce::dontSendNotification);
                recorderStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
                pendingBind = BindTarget::None;
                pendingBindSlot = -1;
                return true;
            }

            const auto desc = key.getTextDescription().toLowerCase();
            if (desc.isEmpty())
                return true; // a lone modifier etc., keep waiting for a real combo

            if (pendingBind == BindTarget::Slot && pendingBindSlot >= 0)
            {
                // Unbind from previous slot
                for (auto& s : soundboard.getSlots())
                    if (juce::String (s.hotkey) == desc) s.hotkey.clear();
                soundboard.getSlots()[(size_t) pendingBindSlot].hotkey = desc.toStdString();
                if (cards[(size_t) pendingBindSlot]) cards[(size_t) pendingBindSlot]->refreshFromSlot();
                if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
                recorderStatusLabel.setText ("Bound \"" + desc.toUpperCase() + "\" to slot " + juce::String (pendingBindSlot + 1) + ".",
                                              juce::dontSendNotification);
                recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
            }
            else if (pendingBind == BindTarget::Record)
            {
                recordHotkey = desc;
                saveRecordHotkeyPreference();
                recorderStatusLabel.setText ("Record hotkey set to \"" + desc.toUpperCase() + "\".", juce::dontSendNotification);
                recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
            }
            pendingBind = BindTarget::None;
            pendingBindSlot = -1;
            refreshGlobalHotkeyWatchList();
            return true;
        }

        const auto desc = key.getTextDescription().toLowerCase();
        if (desc.isEmpty())
            return false;

        // ── easter egg: spam Ctrl+P to open the DOCTOS video (i removed this bs lol)─────────────
        if (key.getModifiers().isCtrlDown() && key.getKeyCode() == (int) 'P')
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastCtrlPTimeMs > 900) // pause too long -> combo resets
                ctrlPComboCount = 0;
            lastCtrlPTimeMs = now;
            ++ctrlPComboCount;
            if (ctrlPComboCount >= 5)
            {
                ctrlPComboCount = 0;
                juce::URL ("https://www.youtube.com/watch?v=i7YfT-ILKAg").launchInDefaultBrowser();
            }
            return true;
        }

        if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == (int) 'J')
        {
            jellyfishOverlay.spawnOne();
            return true;
        }

        if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == (int) 'M')
        {
            toggleModderView();
            return true;
        }

        if (triggerHotkeyByDescription (desc))
            return true;

        if (modernUi && switchTabByShortcut (key))
            return true;

        return false; // not one of ours, let normal focus/navigation handling have it
    }
    int ctrlPComboCount = 0;
    juce::uint32 lastCtrlPTimeMs = 0;

    bool triggerHotkeyByDescription (const juce::String& desc)
    {
        if (recordHotkey.isNotEmpty() && desc == recordHotkey)
        {
            toggleRecording();
            return true;
        }

        for (int i = 0; i < (int) soundboard.getSlots().size(); ++i)
        {
            auto& s = soundboard.getSlots()[(size_t) i];
            if (! s.hotkey.empty() && juce::String (s.hotkey) == desc)
            {
                soundboard.play (i, sbDefaultSpeed, sbDefaultVolume, sbDefaultLoop, sbDefaultReverse);
                return true;
            }
        }

        if (appModHost.handleKeyPress (desc))
            return true;

        return false;
    }

    void refreshGlobalHotkeyWatchList()
    {
        juce::StringArray descriptions;
        if (recordHotkey.isNotEmpty())
            descriptions.add (recordHotkey);
        for (auto& s : soundboard.getSlots())
            if (! s.hotkey.empty())
                descriptions.add (juce::String (s.hotkey));
        globalHotkeys.setWatchList (descriptions);
    }

    void beginHotkeyBind (int slotIdx)
    {
        pendingBind = BindTarget::Slot;
        pendingBindSlot = slotIdx;
        recorderStatusLabel.setText ("Press a key combo for slot " + juce::String (slotIdx + 1) + "... (Esc to cancel)",
                                      juce::dontSendNotification);
        recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
        grabKeyboardFocus();
    }

    void beginRecordHotkeyBind()
    {
        pendingBind = BindTarget::Record;
        recorderStatusLabel.setText ("Press a key combo for Start/Stop Recording... (Esc to cancel)", juce::dontSendNotification);
        recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
        grabKeyboardFocus();
    }

    void unbindHotkey (int slotIdx)
    {
        if (pendingBind == BindTarget::Slot && pendingBindSlot == slotIdx)
        {
            pendingBind = BindTarget::None;
            pendingBindSlot = -1;
        }
        auto& slot = soundboard.getSlots()[(size_t) slotIdx];
        if (slot.hotkey.empty())
        {
            recorderStatusLabel.setText ("Slot " + juce::String (slotIdx + 1) + " has no hotkey to remove.", juce::dontSendNotification);
            recorderStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
            return;
        }
        slot.hotkey.clear();
        if (cards[(size_t) slotIdx]) cards[(size_t) slotIdx]->refreshFromSlot();
        if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
        refreshGlobalHotkeyWatchList();
        recorderStatusLabel.setText ("Removed the hotkey from slot " + juce::String (slotIdx + 1) + ".", juce::dontSendNotification);
        recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
    }

    void unbindRecordHotkey()
    {
        if (pendingBind == BindTarget::Record)
            pendingBind = BindTarget::None;
        if (recordHotkey.isEmpty())
        {
            recorderStatusLabel.setText ("No record hotkey to remove.", juce::dontSendNotification);
            recorderStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
            return;
        }
        recordHotkey.clear();
        saveRecordHotkeyPreference();
        refreshGlobalHotkeyWatchList();
        recorderStatusLabel.setText ("Record hotkey removed.", juce::dontSendNotification);
        recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
    }

    void setRecordingActive (bool on)
    {
        if (on == soundboard.isRecording.load())
            return; // avoid a double start/stop if the toggle click and a hotkey race

        if (on)
        {
            int target = recordTargetSlot;
            if (target < 0)
            {
                target = -1;
                for (int i = 0; i < (int) soundboard.getSlots().size(); ++i)
                    if (soundboard.getSlots()[(size_t) i].path.empty() && soundboard.getSlots()[(size_t) i].data == nullptr) { target = i; break; }
                if (target < 0)
                {
                    recorderStatusLabel.setText ("No empty slot available, pick a target slot first.", juce::dontSendNotification);
                    recorderStatusLabel.setColour (juce::Label::textColourId, palette().danger);
                    if (recordToggleBtn) recordToggleBtn->setToggleState (false, juce::dontSendNotification);
                    return;
                }
            }
            soundboard.startRecording (target);
            recordStartMillis = juce::Time::getMillisecondCounterHiRes();
            if (recordToggleBtn) recordToggleBtn->setToggleState (true, juce::dontSendNotification);
        }
        else
        {
            soundboard.stopRecording (recFolder);
            if (recordToggleBtn) recordToggleBtn->setToggleState (false, juce::dontSendNotification);
            recorderStatusLabel.setText ("Recording saved to " + recFolder.getFileName() + "/.", juce::dontSendNotification);
            recorderStatusLabel.setColour (juce::Label::textColourId, palette().accent);
            for (auto& c : cards)
                if (c) c->refreshFromSlot(); // whichever slot just got the new file shows up
        }
    }

    void toggleRecording() { setRecordingActive (! soundboard.isRecording.load()); }

    void saveRecordHotkeyPreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("hotkey", recordHotkey);
        recordHotkeyPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("recordHotkey");
    }

    void loadRecordHotkeyPreference()
    {
        if (! recordHotkeyPrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (recordHotkeyPrefFile);
        if (parsed.isObject())
            recordHotkey = parsed.getProperty ("hotkey", juce::var()).toString();
    }

    void saveStartupSoundPreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("enabled", startupSoundEnabled);
        obj->setProperty ("path", startupSoundFile.getFullPathName());
        startupSoundPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("startupSound");
    }

    void loadStartupSoundPreference()
    {
        startupSoundFile = juce::File::getCurrentWorkingDirectory().getChildFile ("assets").getChildFile ("windows_95.mp3");
        if (! startupSoundPrefFile.existsAsFile())
            return; // keep the defaults set above / at declaration
        auto parsed = juce::JSON::parse (startupSoundPrefFile);
        if (! parsed.isObject())
            return;
        startupSoundEnabled = (bool) parsed.getProperty ("enabled", true);
        const auto savedPath = parsed.getProperty ("path", juce::var()).toString();
        if (savedPath.isNotEmpty())
            startupSoundFile = juce::File (savedPath);
    }

    void loadStartupJingle()
    {
        startupSoundFormatManager.registerBasicFormats();
        juce::File::getCurrentWorkingDirectory().getChildFile ("assets").createDirectory();
        startupJingleBuffer = startupSoundFile.existsAsFile()
            ? vlab::dsp::loadAudioFileAsStereo (startupSoundFile, startupSoundFormatManager)
            : nullptr;
    }

    static juce::String formatElapsedSeconds (double seconds)
    {
        const int total = (int) seconds;
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void applyAllExperimental()
    {
        refreshRgbTargetHz();
        applyExperimentalLowLatency (experimentalEnabled && experimentalLowLatency);
        updateExperimentalRowsEnabled();
    }

    void applyExperimentalLowLatency (bool on)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = on ? 128 : 0; // 0 == let the driver pick its own default
        const auto err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty())
        {
            experimentalStatusLabel.setText ("Low-latency buffer failed (" + err + "), reverted.", juce::dontSendNotification);
            experimentalStatusLabel.setColour (juce::Label::textColourId, palette().danger);
            experimentalLowLatency = false;
            if (experimentalLowLatencyToggle) experimentalLowLatencyToggle->setToggleState (false, juce::dontSendNotification);
        }
    }

    void updateExperimentalRowsEnabled()
    {
        for (auto* t : { experimentalUncappedRgbToggle, experimentalLowLatencyToggle, experimentalAutoFocusToggle })
        {
            if (t == nullptr) continue;
            t->setEnabled (experimentalEnabled);
            t->setAlpha (experimentalEnabled ? 1.0f : 0.45f);
        }
        if (experimentalStatusLabel.getText().isEmpty() || ! experimentalEnabled)
        {
            experimentalStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
            experimentalStatusLabel.setText (experimentalEnabled
                ? "Experimental features are ON. Turn the master toggle off any time to fall back to normal behaviour."
                : "Experimental features are OFF (default, recommended).",
                juce::dontSendNotification);
        }
    }

    void saveExperimentalPreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("enabled", experimentalEnabled);
        obj->setProperty ("uncappedRgb", experimentalUncappedRgb);
        obj->setProperty ("lowLatency", experimentalLowLatency);
        obj->setProperty ("autoReclaimFocus", experimentalAutoReclaimFocus);
        experimentalPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("experimental");
    }

    void loadExperimentalPreference()
    {
        if (! experimentalPrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (experimentalPrefFile);
        if (! parsed.isObject())
            return;
        experimentalEnabled = (bool) parsed.getProperty ("enabled", false);
        experimentalUncappedRgb = (bool) parsed.getProperty ("uncappedRgb", false);
        experimentalLowLatency = (bool) parsed.getProperty ("lowLatency", false);
        experimentalAutoReclaimFocus = (bool) parsed.getProperty ("autoReclaimFocus", false);
    }

    static constexpr int kPerformanceBufferSizes[] = { 0, 64, 128, 256, 512, 1024, 2048 };
    static constexpr int kPerformanceUiHz[] = { 10, 20, 30 };

    void refreshRgbTargetHz()
    {
        int hz = (experimentalEnabled && experimentalUncappedRgb) ? 144 : 60;
        if (performanceReducedEffects) hz = 15;
        rgbEngine.setTargetHz (hz);
    }

    void applyAllPerformance()
    {
        applyPerformanceBufferSize (performanceBufferSizeIndex);
        applyPerformanceReducedEffects (performanceReducedEffects);
        startTimerHz (kPerformanceUiHz[juce::jlimit (0, 2, performanceUiRateIndex)]);
    }

    void applyPerformanceBufferSize (int idx)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = kPerformanceBufferSizes[juce::jlimit (0, 6, idx)];
        const auto err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty())
        {
            performanceStatusLabel.setText ("Buffer size change failed (" + err + ").", juce::dontSendNotification);
            performanceStatusLabel.setColour (juce::Label::textColourId, palette().danger);
        }
        else
        {
            performanceStatusLabel.setText (setup.bufferSize == 0
                ? "Buffer: driver default"
                : "Buffer: " + juce::String (setup.bufferSize) + " samples",
                juce::dontSendNotification);
            performanceStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        }
    }

    void applyPerformanceReducedEffects (bool on)
    {
        refreshRgbTargetHz();
        jellyfishOverlay.setVisible (! on);
        backgroundLayer.setVisible (! on);
    }

    void savePerformancePreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("bufferSizeIndex", performanceBufferSizeIndex);
        obj->setProperty ("reducedEffects", performanceReducedEffects);
        obj->setProperty ("uiRateIndex", performanceUiRateIndex);
        performancePrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("performance");
    }

    void loadPerformancePreference()
    {
        if (! performancePrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (performancePrefFile);
        if (! parsed.isObject())
            return;
        performanceBufferSizeIndex = (int) parsed.getProperty ("bufferSizeIndex", 0);
        performanceReducedEffects = (bool) parsed.getProperty ("reducedEffects", false);
        performanceUiRateIndex = (int) parsed.getProperty ("uiRateIndex", 1);
    }

    void saveDevicePreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("mic", micBox.getText());
        obj->setProperty ("out", outBox.getText());
        obj->setProperty ("mon", monBox.getText());
        devicePrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("device");
    }

    void loadDevicePreference()
    {
        if (! devicePrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (devicePrefFile);
        if (! parsed.isObject())
            return;
        savedMicDeviceName = parsed.getProperty ("mic", juce::String()).toString();
        savedOutDeviceName = parsed.getProperty ("out", juce::String()).toString();
        savedMonDeviceName = parsed.getProperty ("mon", juce::String()).toString();
    }

    // ── audio ─────────────────────────────────────────────────────────────
    void prepareToPlay (int samplesPerBlockExpected, double) override
    {
        engine.prepare (vlab::kSampleRate, samplesPerBlockExpected);
        inBuf.setSize (2, samplesPerBlockExpected);
        outBuf.setSize (2, samplesPerBlockExpected);
        statsPanel.bufferSize = samplesPerBlockExpected;
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        const int n = bufferToFill.numSamples;
        inBuf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = std::min (ch, bufferToFill.buffer->getNumChannels() - 1);
            inBuf.copyFrom (ch, 0, *bufferToFill.buffer, srcCh, bufferToFill.startSample, n);
        }
        outBuf.setSize (2, n, false, false, true);
        soundboard.feedRecordingInput (inBuf, n);
        engine.process (inBuf, outBuf, n); // real mic audio through the entire FX chain
        if (! running.load())
            outBuf.clear(); // "Stop Engine" mutes output; device stays open and routed
        if (micMonitorEnabled)
            monitorOut.push (outBuf, n);

        for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
            bufferToFill.buffer->copyFrom (ch, bufferToFill.startSample, outBuf, std::min (ch, 1), 0, n);

        // ── startup jingle: mixed in post-FX so it plays cleanly regardless
        // of mic FX/engine state, and stops on its own once it's done. ──
        if (startupJingleActive.load() && startupJingleBuffer != nullptr)
        {
            const int total = startupJingleBuffer->getNumSamples();
            int pos = startupJinglePos.load();
            const int toCopy = juce::jmin (n, total - pos);
            if (toCopy > 0)
            {
                for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
                    bufferToFill.buffer->addFrom (ch, bufferToFill.startSample,
                        *startupJingleBuffer, std::min (ch, 1), pos, toCopy, 0.7f);
            }
            pos += n;
            if (pos >= total)
                startupJingleActive = false;
            else
                startupJinglePos = pos;

            for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
            {
                auto* d = bufferToFill.buffer->getWritePointer (ch, bufferToFill.startSample);
                for (int i = 0; i < n; ++i)
                    d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
            }
        }
    }

    void releaseResources() override {}

    // ── layout ────────────────────────────────────────────────────────────
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient bg (palette().bg.brighter (0.015f), 0, 0, palette().bg.darker (0.2f), 0, b.getHeight(), false);
        g.setGradientFill (bg);
        g.fillAll();
        g.setColour (palette().stroke);
        g.drawVerticalLine (getSidebarWidth(), 0.0f, (float) getHeight());

        if (modernUi)
        {
            const int stripTop = 14;
            const int stripBottom = stripTop + 40;
            g.setColour (palette().stroke.withAlpha (0.7f));
            g.fillRect (getSidebarWidth() + 14, stripBottom, getWidth() - getSidebarWidth() - 28, 1);

            if (auto* active = getActiveTabButton())
            {
                const auto r = active->getBounds();
                g.setColour (palette().accent);
                g.fillRoundedRectangle ((float) r.getX() + 8.0f, (float) stripBottom - 2.0f, (float) r.getWidth() - 16.0f, 3.0f, 1.5f);
            }
        }
    }

    void resized() override
    {
        backgroundLayer.setBounds (getLocalBounds());

        auto area = getLocalBounds();
        sidebarArea = area.removeFromLeft (getSidebarWidth());
        layoutSidebar (sidebarArea.reduced (modernUi ? 16 : 20));

        auto main = area.reduced (modernUi ? 14 : 18);
        auto topBar = main.removeFromTop (modernUi ? 40 : 44);
        layoutTopBar (topBar);
        main.removeFromTop (modernUi ? 14 : 14);

        auto boardArea = main;
        boardSelectorBar.setBounds (boardArea.removeFromTop (modernUi ? 52 : 56));
        boardArea.removeFromTop (modernUi ? 8 : 10);
        if (modernUi)
        {
            auto searchRow = boardArea.removeFromTop (32);
            if (importButton.isVisible())
            {
                importButton.setBounds (searchRow.removeFromRight (80));
                searchRow.removeFromRight (8);
            }
            searchBox.setBounds (searchRow);
            boardArea.removeFromTop (8);
        }
        if (tabEchoPanel_ != nullptr)
        {
            tabEchoPanel_->setBounds (boardArea.removeFromTop (64));
            boardArea.removeFromTop (10);
        }
        {
            auto row = boardArea.removeFromTop (28);
            boardSettingsLabel.setBounds (row.removeFromLeft (juce::jmin (140, row.getWidth() / 5)));
            row.removeFromLeft (8);
            boardVolumeLabel.setBounds (row.removeFromLeft (44));
            boardVolumeSlider.setBounds (row.removeFromLeft (juce::jmax (80, row.getWidth() / 4)));
            row.removeFromLeft (10);
            boardSpeedLabel.setBounds (row.removeFromLeft (40));
            boardSpeedSlider.setBounds (row.removeFromLeft (juce::jmax (80, row.getWidth() / 3)));
            row.removeFromLeft (10);
            boardLoopToggle.setBounds (row.removeFromLeft (70));
            row.removeFromLeft (6);
            boardReverseToggle.setBounds (row.removeFromLeft (80));
            boardArea.removeFromTop (10);
        }
        boardViewport.setBounds (boardArea);
        fxRackViewport.setBounds (main);
        settingsViewport.setBounds (main);
        visualizerViewport.setBounds (main);
        modsViewport.setBounds (main);
        synthViewport.setBounds (main);
        themeCreatorViewport.setBounds (main);
        ttsViewport.setBounds (main);
        appModFullViewHost.setBounds (main);
        if (activeFullViewCanvas != nullptr) activeFullViewCanvas->setBounds (appModFullViewHost.getLocalBounds());
        layoutBoardContent (boardViewport.getWidth());
        layoutFxRackContent (fxRackViewport.getWidth());
        layoutSettingsContent (settingsViewport.getWidth());
        layoutVisualizerContent (visualizerViewport.getWidth(), visualizerViewport.getHeight());
        layoutSynthContent (synthViewport.getWidth());
        layoutModsContent (modsViewport.getWidth());
        layoutThemeCreatorContent (themeCreatorViewport.getWidth());
        layoutTtsContent (ttsViewport.getWidth());

        jellyfishOverlay.setBounds (getLocalBounds());
        if (modderView != nullptr) modderView->setBounds (getLocalBounds());
    }

private:
    // engine + subsystems
    vlab::AudioEngine engine;
    vlab::SoundboardCore soundboard;
    vlab::LiveTts tts { soundboard };
    vlab::RealtimeSynth synth;
    vlab::SynthParams synthParams;
    std::unique_ptr<vlab::SongSequencer> sequencer;
    vlab::MonitorOutput monitorOut;
    vlab::SystemAudioCapture systemAudioCapture;
    std::unique_ptr<vlab::ui::TabEchoPanel> tabEchoPanel_; // compact echo controls, shown on the Board tab
    vlab::SaveStore saveStore; // one consolidated JSON file backing every preference/config file below
    juce::File recFolder, configFile, modsFolder, themesFolder, themePrefFile, uiPresetPrefFile, backgroundPrefFile, lastBackgroundPath, rgbPrefFile, recordHotkeyPrefFile;
    juce::File splashStylePrefFile;
    int splashStyleIndex = 0; // see vlab::ui::SplashStyle
    juce::File languagesFolder, languagePrefFile;
    juce::File arpSongsFile, modSongsFolder;
    juce::File deletedRecordingsFile;

    std::vector<std::string> heldArpNotes;
    std::mt19937 arpRng { std::random_device{}() };
    juce::TextButton reloadSongsButton { "Reload Songs" };

    // ── in-app hotkeys ──
    enum class BindTarget { None, Slot, Record };
    BindTarget pendingBind = BindTarget::None;
    int pendingBindSlot = -1;
    juce::String recordHotkey; // canonical juce::KeyPress::getTextDescription(), lowercase; empty == unset
    vlab::ui::GlobalHotkeyWatcher globalHotkeys; // same combos, but working while the app isn't focused

    // ── recording ──
    int recordTargetSlot = -1; // -1 == "next empty slot"
    double recordStartMillis = 0.0;
    juce::ToggleButton* recordToggleBtn = nullptr; // owned by the Settings FxSection row
    juce::Label recorderStatusLabel;
    juce::TextButton setRecordHotkeyButton { "Set Record Hotkey" };
    juce::uint32 lastRecordHotkeyBtnClickMs = 0;

    // ── experimental features (opt-in, off by default) ──
    juce::File experimentalPrefFile;
    bool experimentalEnabled = false;
    bool experimentalUncappedRgb = false;
    bool experimentalLowLatency = false;
    bool experimentalAutoReclaimFocus = false;
    juce::ToggleButton* experimentalUncappedRgbToggle = nullptr;
    juce::ToggleButton* experimentalLowLatencyToggle = nullptr;

    juce::File performancePrefFile;
    int performanceBufferSizeIndex = 0; // index into kPerformanceBufferSizes, 0 == driver default
    bool performanceReducedEffects = false;
    int performanceUiRateIndex = 1; // 0 == Low/10Hz, 1 == Normal/20Hz, 2 == High/30Hz
    juce::Label performanceStatusLabel;

    juce::File devicePrefFile;
    juce::String savedMicDeviceName, savedOutDeviceName, savedMonDeviceName;
    juce::ToggleButton* experimentalAutoFocusToggle = nullptr;
    juce::Label experimentalWarningLabel, experimentalStatusLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    vlab::AudioBuffer inBuf, outBuf;

    // ── startup sound (toggleable + user-choosable in Settings) ──
    juce::File startupSoundPrefFile;
    juce::AudioFormatManager startupSoundFormatManager;
    std::shared_ptr<vlab::AudioBuffer> startupJingleBuffer; // nullptr if no asset is loaded
    std::atomic<int> startupJinglePos { 0 };
    std::atomic<bool> startupJingleActive { false };
    bool startupSoundEnabled = true;
    juce::File startupSoundFile; // currently chosen file, may not exist yet on first run
    juce::Label startupSoundStatusLabel;
    juce::TextButton chooseStartupSoundButton { "Choose Startup Sound..." };

    std::atomic<bool> micMonitorEnabled { true };
    std::atomic<float> sbDefaultVolume { 1.0f }, sbDefaultSpeed { 1.0f };
    std::atomic<bool> sbDefaultLoop { false }, sbDefaultReverse { false };

    vlab::ui::LookAndFeel lookAndFeel;
    vlab::ui::ThemeManager themeManager { lookAndFeel };
    vlab::ui::UiPresetManager uiPresetManager;
    vlab::ui::TranslationManager translationManager;
    vlab::ui::RgbEngine rgbEngine { lookAndFeel, themeManager };
    vlab::ModHost modHost;
    std::function<void (const juce::String&, int, int)> reportLoadProgress;

    void reportProgress (const juce::String& label, int done, int total)
    {
        if (reportLoadProgress) reportLoadProgress (label, done, total);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }

public:
    void loadModsInBackground (std::function<void()> onAllDone)
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);

        auto postProgress = [safeThis] (const juce::String& label, int done, int total)
        {
            juce::MessageManager::callAsync ([safeThis, label, done, total]
            {
                if (safeThis == nullptr) return;
                if (safeThis->reportLoadProgress) safeThis->reportLoadProgress (label, done, total);
            });
        };

        const auto modsFolderCopy = modsFolder;
        const auto vizFolderCopy = visualizersFolder;
        const double sampleRate = vlab::kSampleRate;

        std::thread ([safeThis, postProgress, modsFolderCopy, vizFolderCopy, sampleRate, onAllDone]
        {
            constexpr int kDllCooldownMs = 500; // spread LoadLibrary calls out, avoids AV-scan CPU spikes

            auto loadedMods = vlab::ModHost::scanDetached (modsFolderCopy, sampleRate, 512,
                [&] (int d, int t) { postProgress ("Loading mods", d, t); }, kDllCooldownMs);

            auto loadedViz = vlab::VisualizerModHost::scanDetached (vizFolderCopy, 320, 260,
                [&] (int d, int t) { postProgress ("Loading visualizer mods", d, t); }, kDllCooldownMs);

            struct ScanResults
            {
                std::vector<std::unique_ptr<vlab::LoadedMod>> mods;
                std::vector<std::unique_ptr<vlab::LoadedVisualizer>> viz;
            };
            auto results = std::make_shared<ScanResults> (ScanResults {
                std::move (loadedMods), std::move (loadedViz) });

            juce::MessageManager::callAsync ([safeThis, onAllDone, results]
            {
                if (safeThis != nullptr)
                {
                    safeThis->modHost.adopt (std::move (results->mods));
                    safeThis->visualizerModHost.adopt (std::move (results->viz));

                    safeThis->rebuildModsList();
                    safeThis->refreshVizChangerCombo();
                    safeThis->showFeaturedVisualizer();
                }
                if (onAllDone) onAllDone();
            });
        }).detach();
    }

    void showToast (const juce::String& text, int durationMs = 1800)
    {
        auto* notice = new juce::Label ({}, text);
        notice->setJustificationType (juce::Justification::centred);
        notice->setColour (juce::Label::backgroundColourId, juce::Colour (0xee121212));
        notice->setColour (juce::Label::textColourId, palette().text);
        notice->setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (420, getWidth() - 40), 40));
        addAndMakeVisible (notice);
        notice->toFront (false);
        juce::Component::SafePointer<juce::Label> safeNotice (notice);
        juce::Timer::callAfterDelay (durationMs, [safeNotice]
        {
            delete safeNotice.getComponent();
        });
    }

    void toggleModderView()
    {
        if (! advancedMode)
        {
            if (reloadOverlay == nullptr) // don't stack a second notice on top of a reload in progress
            {
                auto* notice = new juce::Label ({}, "Modder View needs Advanced Mode (Settings -> General) switched on first.");
                notice->setJustificationType (juce::Justification::centred);
                notice->setColour (juce::Label::backgroundColourId, juce::Colour (0xee121212));
                notice->setColour (juce::Label::textColourId, palette().text);
                notice->setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 40));
                addAndMakeVisible (notice);
                notice->toFront (false);
                juce::Component::SafePointer<juce::Label> safeNotice (notice);
                juce::Timer::callAfterDelay (2200, [safeNotice]
                {
                    delete safeNotice.getComponent(); // Component's destructor detaches itself from its parent
                });
            }
            return;
        }

        if (modderView != nullptr)
        {
            removeChildComponent (modderView.get());
            modderView = nullptr;
        }
        else
        {
            modderView = std::make_unique<vlab::ui::ModderView> (*this);
            addAndMakeVisible (*modderView);
            modderView->setBounds (getLocalBounds());
            modderView->toFront (true);
            modderView->grabKeyboardFocus();
        }
    }
private:
    vlab::UpdateChecker updateChecker;
    std::unique_ptr<vlab::ui::ModderView> modderView;
    vlab::ui::JellyfishOverlay jellyfishOverlay;
    vlab::ui::BackgroundLayer backgroundLayer;
    vlab::DiscordRichPresence discordRPC { "1532597752410935397" };
    bool discordRpcEnabled = true;
    juce::File discordRpcPrefFile;
    juce::uint32 lastDiscordBoardRefreshMs = 0;
    int discordNowPlayingSlot = -1;
    juce::String lastGenericDiscordText;
    bool modernUi = true;
    juce::File modernUiPrefFile;
    int getSidebarWidth() const { return modernUi ? 264 : 300; }
    juce::Rectangle<int> sidebarArea;

    // sidebar widgets
    juce::Label logoLabel { {}, "V LAB" }, taglineLabel { {}, "Modulate everything." };
    juce::Label editionLabel { {}, "Lite \xe2\x80\x94 C++ Rebuild" };
    juce::Label micLabel { {}, "Microphone" }, outLabel { {}, "Output (Cable)" }, monLabel { {}, "Monitor" };
    juce::ComboBox micBox, outBox, monBox;
    juce::Slider monitorVolumeSlider;
    juce::TextButton refreshButton { "Refresh Devices" };
    juce::TextButton startButton { "Start Engine" };
    juce::Label routingStatusLabel;
    std::atomic<bool> running { true };

    juce::Label cpuLabel { {}, "CPU 0%" };
    juce::ToggleButton micMonitorToggle { "Mic Monitor" };
    juce::Label vuTitleLabel { {}, "VU" };
    struct DotVu : public juce::Component
    {
        float l = 0.0f, r = 0.0f;
        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const int dots = 20;
            const float w = (float) getWidth() / dots;
            for (int i = 0; i < dots; ++i)
            {
                const float t = (float) i / dots;
                const bool onL = t < l, onR = t < r;
                juce::Colour c = t > 0.85f ? p.danger : (t > 0.6f ? juce::Colour (0xffe6c93f) : p.accent);
                g.setColour ((onL || onR) ? c : p.panelAlt);
                g.fillRoundedRectangle (i * w + 1, 0.0f, w - 2, (float) getHeight(), 2.0f);
            }
        }
    } vuBar;

    struct PianoKeyButton : public juce::Button
    {
        bool isBlack = false;
        std::function<void()> onPress, onRelease;
        PianoKeyButton() : juce::Button ({}) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto& p = palette();
            auto b = getLocalBounds().toFloat();
            juce::Colour top = isBlack ? juce::Colour (0xff1a1a1a) : juce::Colour (0xffe8e8e8);
            juce::Colour bot = isBlack ? juce::Colour (0xff050505) : juce::Colour (0xffc4c4c4);
            if (down || getToggleState()) { top = p.accent.brighter (0.2f); bot = p.accent.darker (0.15f); }
            else if (highlighted) { top = top.brighter (0.1f); bot = bot.brighter (0.05f); }
            juce::ColourGradient grad (top, 0, 0, bot, 0, b.getHeight(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override { juce::Button::mouseDown (e); if (onPress) onPress(); }
        void mouseUp (const juce::MouseEvent& e) override { juce::Button::mouseUp (e); if (onRelease) onRelease(); }
    };
    std::vector<std::unique_ptr<PianoKeyButton>> pianoKeyButtons;

    juce::Label presetLabel { {}, "Quick Preset" };
    juce::ComboBox presetBox;
    juce::Label songLabel { {}, "Quick Song" };
    juce::ComboBox songBox;
    juce::TextButton stopSongButton { "Stop Song" };

    // top bar
    juce::TextEditor searchBox;
    juce::TextButton stopAllSoundsButton { "Stop All Sounds" };
    juce::Label boardSettingsLabel { {}, "Playback defaults:" };
    juce::Label boardVolumeLabel { {}, "Volume" }, boardSpeedLabel { {}, "Speed" };
    juce::Slider boardVolumeSlider, boardSpeedSlider;
    juce::ToggleButton boardLoopToggle { "Loop" }, boardReverseToggle { "Reverse" };
    juce::TextButton importButton { "Import" };
    juce::TextButton fxRackTabButton { "FX Rack" }, boardTabButton { "Board" };
    juce::TextButton settingsTabButton { "Settings" }, visualizerTabButton { "Visualizer" };
    juce::TextButton modsTabButton { "Mods" }, synthTabButton { "Synth" };
    juce::TextButton themeCreatorTabButton { "Themes" };
    juce::TextButton ttsTabButton { "TTS" };
    enum class Tab { Board, FxRack, Settings, Visualizer, Mods, Synth, ThemeCreator, Tts };
    Tab currentTab = Tab::Board;
    Tab lastGenericDiscordTab = Tab::Board;

    class ReloadOverlay : public juce::Component
    {
    public:
        juce::String statusText { "Reloading mods..." };
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.55f));
            auto box = getLocalBounds().withSizeKeepingCentre (juce::jmin (360, getWidth() - 40), 64);
            g.setColour (juce::Colour (0xff0a1a3a));
            g.fillRoundedRectangle (box.toFloat(), 10.0f);
            g.setColour (juce::Colour (0xff2e8bff).withAlpha (0.5f));
            g.drawRoundedRectangle (box.toFloat().reduced (1.0f), 10.0f, 1.5f);
            g.setColour (juce::Colour (0xffbfe0ff));
            g.setFont (juce::Font (14.0f, juce::Font::bold));
            g.drawText (statusText, box.reduced (12, 0), juce::Justification::centred);
        }
    };
    std::unique_ptr<ReloadOverlay> reloadOverlay;

    juce::String tabDisplayName (Tab tab)
    {
        switch (tab)
        {
            case Tab::Board:        return "Board";
            case Tab::FxRack:       return "FX Rack";
            case Tab::Settings:     return "Settings";
            case Tab::Visualizer:   return "Visualizer";
            case Tab::Mods:         return "Mods";
            case Tab::Synth:        return "Synth";
            case Tab::ThemeCreator: return "Theme Creator";
            case Tab::Tts:          return "TTS";
        }
        return "vlab";
    }

    bool advancedMode = false;

    // board tab
    vlab::ui::BoardSelectorBar boardSelectorBar; // the row of soundboard "pages" above the grid
    vlab::ui::SmoothViewport boardViewport;
    juce::Component boardContent;
    std::array<std::unique_ptr<SoundSlotCard>, kVisibleSlots> cards;

    // fx rack tab: scrollable stack of section cards
    vlab::ui::SmoothViewport fxRackViewport;
    juce::Component fxRackContent;
    std::vector<std::unique_ptr<FxSection>> fxSections;
    juce::TextButton fxResetButton { "Reset All FX to Defaults" };
    juce::TextButton fxCollapseAllButton { "Collapse All" }, fxExpandAllButton { "Expand All" };
    juce::ComboBox fxPresetCombo;
    juce::TextButton fxSavePresetButton { "Save Preset..." };
    juce::TextButton fxDeletePresetButton { "Delete" };
    juce::File fxPresetsFile;
    juce::StringArray fxPresetNames; // mirrors fxPresetCombo's items, index+1 == item ID
    std::vector<std::unique_ptr<FxSection>> modFxSections; // dynamic, one per mod with parameters

    // settings tab
    vlab::ui::SmoothViewport settingsViewport;
    juce::Component settingsContent;
    std::vector<std::unique_ptr<FxSection>> settingsSections;
    VirtualHeadWidget settingsHeadWidget;
    juce::Label backgroundStatusLabel;
    juce::TextButton loadBackgroundButton { "Load Background Image/GIF" }, clearBackgroundButton { "Clear Background" };
    juce::Label sbFolderLabel;
    juce::TextButton openRecordingsFolderButton { "Open Recordings Folder" };

    // visualizer tab (Advanced Mode only)
    vlab::ui::SmoothViewport visualizerViewport;
    juce::Component visualizerContent;
    WaveformDisplay waveformDisplay;
    SpectrumAnalyzer spectrumAnalyzer;
    Goniometer goniometer;
    VuMeterDisplay vuMeterDisplay;
    RadialSpectrum radialSpectrum;
    VirtualHeadWidget visualizerHeadWidget;
    juce::Label visualizerTitleLabel { {}, "Live Signal" };
    juce::ComboBox vizChangerCombo;
    enum class VizMode { Waveform, Spectrum, Goniometer, VuMeter, Radial, Head, Mod };
    VizMode currentVizMode = VizMode::Waveform;
    int selectedVizModIndex = -1;
    vlab::VisualizerModHost visualizerModHost;
    vlab::AppModHost appModHost;
    juce::File appModsFolder;
    juce::File visualizersFolder;

    std::vector<std::unique_ptr<FxSection>> appModsSections;
    std::vector<std::unique_ptr<vlab::ui::AppModCanvasComponent>> appModEmbeddedCanvases;
    struct AppModFullViewTab
    {
        std::unique_ptr<vlab::ui::AppModCanvasComponent> canvas;
        std::unique_ptr<juce::TextButton> button;
    };
    std::vector<std::unique_ptr<AppModFullViewTab>> appModFullViewTabs;
    vlab::ui::AppModCanvasComponent* activeFullViewCanvas = nullptr;
    juce::Component appModFullViewHost; // sized to `main`; holds whichever full-view canvas is active

    // synth tab
    vlab::ui::SmoothViewport synthViewport;
    juce::Component synthContent;
    std::vector<std::unique_ptr<FxSection>> synthSections;
    juce::ComboBox* synthPlaySongCombo = nullptr;
    juce::TextButton synthPlayButton { "Play Song" }, synthStopButton { "Stop Song / Note" };
    juce::TextButton synthResetButton { "Reset Patch" };
    juce::Label keyboardLabel;
    juce::Label toneLabel;
    juce::ToggleButton holdToneToggle { "Hold Tone" };
    juce::Slider toneFreqSlider;

    // theme creator tab
    vlab::ui::SmoothViewport themeCreatorViewport;
    juce::Component themeCreatorContent;
    std::unique_ptr<ThemeCreatorPanel> themeCreatorPanel;
    std::unique_ptr<RgbControlPanel> rgbControlPanel;

    vlab::ui::SmoothViewport ttsViewport;
    juce::Component ttsContent;
    std::vector<std::unique_ptr<FxSection>> ttsSections;
    juce::Label ttsLabel { {}, "Text-to-Speech" };
    juce::TextEditor ttsInput;
    juce::TextButton speakButton { "Speak" }, stopTtsButton { "Stop TTS" };

    vlab::ui::SmoothViewport modsViewport;
    juce::Component modsContent;
    std::vector<std::unique_ptr<FxSection>> modsSections;
    juce::Label modsIntroLabel, modsSdkLabel, appModsIntroLabel;
    juce::TextButton rescanModsButton { "Reload All Mods" }, openModsFolderButton { "Open Mods Folder" };

    struct TimerHelper : public juce::Timer { std::function<void()> cb; void timerCallback() override { if (cb) cb(); } };
    TimerHelper timer;
    void startTimerHz (int hz) { timer.cb = [this] { refreshMeters(); }; timer.startTimerHz (hz); }

    void refreshMeters()
    {
        vuBar.l = engine.vuPeakL; vuBar.r = engine.vuPeakR; vuBar.repaint();
        cpuLabel.setText ("CPU " + juce::String ((int) engine.cpuPercent.load()) + "%", juce::dontSendNotification);

        appModHost.tick(); // let any loaded App Mods poll/update their own state
        for (auto& c : appModEmbeddedCanvases) if (c->isVisible()) c->refreshFrame();
        for (auto& t : appModFullViewTabs) if (t->canvas->isVisible()) t->canvas->refreshFrame();

        for (auto& c : cards)
            if (c) c->pollProgress();

        if (const int skipped = soundboard.takeSkippedForMemoryCount())
            showToast (juce::String (skipped) + " sounds were not preloaded (memory limit). They load when you play them.");

        if (currentTab == Tab::Board || currentTab == Tab::Tts)
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastDiscordBoardRefreshMs >= 750)
            {
                lastDiscordBoardRefreshMs = now;
                refreshDiscordPresence();
            }
        }

        if (soundboard.isRecording.load())
        {
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - recordStartMillis) / 1000.0;
            recorderStatusLabel.setText ("Recording... " + formatElapsedSeconds (elapsed), juce::dontSendNotification);
            recorderStatusLabel.setColour (juce::Label::textColourId, palette().danger);
        }

        if (experimentalEnabled && experimentalAutoReclaimFocus && pendingBind == BindTarget::None)
        {
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            const bool textEditingFocused = dynamic_cast<juce::TextEditor*> (focused) != nullptr;
            if (! textEditingFocused && focused != this)
                grabKeyboardFocus();
        }

        if (currentTab == Tab::Visualizer)
        {
            statsPanel.repaint();
            switch (currentVizMode)
            {
                case VizMode::Waveform:   waveformDisplay.setSamples (engine.visData); break;
                case VizMode::Spectrum:   spectrumAnalyzer.setSamples (engine.visData); break;
                case VizMode::Goniometer: goniometer.setSamples (engine.visData, engine.visDataR); break;
                case VizMode::VuMeter:    vuMeterDisplay.setSamples (engine.visData, engine.visDataR); break;
                case VizMode::Radial:     radialSpectrum.setSamples (engine.visData); break;
                case VizMode::Head:       break; // no per-frame data needed, it's just the draggable widget
                case VizMode::Mod:
                    if (selectedVizModIndex >= 0 && modVizDisplay.getWidth() > 0)
                    {
                        modVizDisplay.image = visualizerModHost.render (selectedVizModIndex, engine.visData, engine.visDataR,
                                                                          modVizDisplay.getWidth(), modVizDisplay.getHeight(),
                                                                          palette().accent);
                        modVizDisplay.repaint();
                    }
                    break;
            }
        }
    }

    // ── build: sidebar ───────────────────────────────────────────────────
    void buildSidebar()
    {
        logoLabel.setFont (juce::Font (30.0f, juce::Font::bold));
        logoLabel.setColour (juce::Label::textColourId, palette().accent);
        taglineLabel.setFont (juce::Font (13.0f));
        taglineLabel.setColour (juce::Label::textColourId, palette().textDim);
        editionLabel.setFont (juce::Font (12.0f, juce::Font::bold));
        editionLabel.setColour (juce::Label::textColourId, palette().accent);

        for (auto* l : { &micLabel, &outLabel, &monLabel, &presetLabel, &songLabel, &vuTitleLabel })
        {
            l->setFont (juce::Font (11.0f));
            l->setColour (juce::Label::textColourId, palette().textDim);
        }
        cpuLabel.setFont (juce::Font (11.0f));
        cpuLabel.setColour (juce::Label::textColourId, palette().textDim);

        for (auto* c : { &micBox, &outBox, &monBox, &presetBox, &songBox })
            addAndMakeVisible (*c);
        for (auto* l : { &logoLabel, &taglineLabel, &editionLabel, &micLabel, &outLabel, &monLabel,
                          &cpuLabel, &vuTitleLabel, &presetLabel, &songLabel })
            addAndMakeVisible (*l);
        addAndMakeVisible (refreshButton);
        addAndMakeVisible (startButton);
        addAndMakeVisible (micMonitorToggle);
        addAndMakeVisible (vuBar);
        addAndMakeVisible (stopSongButton);

        startButton.getProperties().set ("primary", true);
        micMonitorToggle.setToggleState (true, juce::dontSendNotification);
        micMonitorToggle.onClick = [this] { micMonitorEnabled = micMonitorToggle.getToggleState(); };

        refreshButton.onClick = [this] { refreshDeviceLists(); };
        startButton.onClick = [this] { toggleEngine(); };
        micBox.onChange = [this] { applyDeviceSelection(); };
        outBox.onChange = [this] { applyDeviceSelection(); };
        monBox.onChange = [this] { monitorOut.setDevice (monBox.getText()); saveDevicePreference(); };

        addAndMakeVisible (monitorVolumeSlider);
        monitorVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        monitorVolumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        monitorVolumeSlider.setRange (0.0, 1.5, 0.01);
        monitorVolumeSlider.setValue (1.0, juce::dontSendNotification);
        monitorVolumeSlider.setTextValueSuffix ("x");
        monitorVolumeSlider.setTooltip ("How loud the Monitor device plays back, independent of the main output");
        monitorVolumeSlider.onValueChange = [this] { monitorOut.setVolume ((float) monitorVolumeSlider.getValue()); };

        addAndMakeVisible (routingStatusLabel);
        routingStatusLabel.setFont (juce::Font (10.5f));
        routingStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        routingStatusLabel.setJustificationType (juce::Justification::topLeft);

        presetBox.addItem ("Clean", 1);
        presetBox.addItem ("Robot", 2);
        presetBox.addItem ("Deep Voice", 3);
        presetBox.addItem ("Chipmunk", 4);
        presetBox.addItem ("Radio", 5);
        presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
        presetBox.onChange = [this] { applyPreset (presetBox.getText()); refreshDiscordPresence(); };

        for (auto& n : buildSongNameList())
            songBox.addItem (n, songBox.getNumItems() + 1);
        songBox.onChange = [this]
        {
            if (sequencer) sequencer->playSong (songBox.getText().toStdString());
            refreshDiscordPresence();
        };
        stopSongButton.getProperties().set ("danger", true);
        stopSongButton.onClick = [this] { if (sequencer) sequencer->stop(); refreshDiscordPresence(); };

        refreshDeviceLists();
    }

    void saveBackgroundPreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("path", lastBackgroundPath.getFullPathName());
        obj->setProperty ("opacity", (double) backgroundLayer.getOpacity());
        backgroundPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("background");
    }

    void loadBackgroundPreference()
    {
        if (! backgroundPrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (backgroundPrefFile);
        if (! parsed.isObject())
            return;

        const juce::String path = parsed.getProperty ("path", juce::String()).toString();
        const float opacity = (float) (double) parsed.getProperty ("opacity", 0.5);
        backgroundLayer.setOpacity (opacity);

        if (path.isNotEmpty())
        {
            juce::File f (path);
            if (f.existsAsFile())
            {
                backgroundLayer.loadFile (f);
                lastBackgroundPath = f;
            }
        }
    }

    void applyDeviceSelection()
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);

        if (micBox.getText().isNotEmpty()) setup.inputDeviceName = micBox.getText();
        if (outBox.getText().isNotEmpty()) setup.outputDeviceName = outBox.getText();
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;

        const auto err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty())
        {
            routingStatusLabel.setText ("Device error: " + err, juce::dontSendNotification);
            routingStatusLabel.setColour (juce::Label::textColourId, palette().danger);
        }
        else
        {
            routingStatusLabel.setText ("Mic: " + setup.inputDeviceName + "\nOut: " + setup.outputDeviceName,
                                         juce::dontSendNotification);
            routingStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        }
        saveDevicePreference();
    }

    void applyPreset (const juce::String& name)
    {
        auto& p = engine.params;
        // Reset to neutral first, then layer the preset on top.
        p.pitch = 0; p.distortion = 0; p.bitcrush = 0; p.reverbMix = 0; p.chorusMix = 0;
        p.eqLo = 0; p.eqMid = 0; p.eqHi = 0; p.tremoloDepth = 0; p.phaserMix = 0;

        if (name == "Clean") { /* neutral, nothing further */ }
        else if (name == "Robot") { p.bitcrush = 6; p.chorusMix = 0.15f; p.eqMid = -3.0f; }
        else if (name == "Deep Voice") { p.pitch = -5.0f; p.eqLo = 4.0f; p.compRatio = 3.0f; }
        else if (name == "Chipmunk") { p.pitch = 7.0f; p.eqHi = 3.0f; }
        else if (name == "Radio") { p.eqLo = -12.0f; p.eqHi = -6.0f; p.distortion = 0.15f; p.bitcrush = 10; }
        refreshFxSlidersFromParams();
    }

    // Sync sliders after preset change
    std::function<void()> refreshFxSlidersFromParams;

    struct GlitchEffectRef { const char* name; std::atomic<bool>* enabled; std::atomic<float>* mix; };
    std::vector<GlitchEffectRef> glitchEffectRefs()
    {
        auto& p = engine.params;
        return {
            { "stutter",             &p.stutterEnabled,          &p.stutterMix },
            { "escalating_repeat",   &p.escRepeatEnabled,        &p.escRepeatMix },
            { "accelerating_repeat", &p.accRepeatEnabled,        &p.accRepeatMix },
            { "decelerate_repeat",   &p.decRepeatEnabled,        &p.decRepeatMix },
            { "pitch_down_repeat",   &p.pitchDownRepeatEnabled,  &p.pitchDownRepeatMix },
            { "scatter",             &p.scatterEnabled,          &p.scatterMix },
            { "reverse_bursts",      &p.reverseBurstEnabled,     &p.reverseBurstMix },
            { "glitch_gate",         &p.glitchGateEnabled,       &p.glitchGateMix },
            { "buffer_freeze",       &p.freezeEnabled,           &p.freezeMix },
            { "vinyl_stop",          &p.vinylStopEnabled,        &p.vinylStopMix },
            { "broken_delay",        &p.brokenDelayEnabled,      &p.brokenDelayMix },
            { "pitch_jumps",         &p.pitchJumpEnabled,        &p.pitchJumpMix },
            { "backmask",            &p.backmaskEnabled,         &p.backmaskMix },
            { "micro_freeze",        &p.microFreezeEnabled,      &p.microFreezeMix },
            { "buffer_shuffle",      &p.bufferShuffleEnabled,    &p.bufferShuffleMix },
        };
    }

    void refreshDeviceLists()
    {
        micBox.clear(); outBox.clear(); monBox.clear();
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        juce::StringArray inputs  = type != nullptr ? type->getDeviceNames (true)  : juce::StringArray();
        juce::StringArray outputs = type != nullptr ? type->getDeviceNames (false) : juce::StringArray();

        int id = 1;
        for (auto& n : inputs)  micBox.addItem (n, id++);
        id = 1;
        for (auto& n : outputs) { outBox.addItem (n, id); monBox.addItem (n, id); ++id; }

        const int savedMicIdx = savedMicDeviceName.isNotEmpty() ? inputs.indexOf (savedMicDeviceName) : -1;
        const int savedOutIdx = savedOutDeviceName.isNotEmpty() ? outputs.indexOf (savedOutDeviceName) : -1;
        const int savedMonIdx = savedMonDeviceName.isNotEmpty() ? outputs.indexOf (savedMonDeviceName) : -1;

        if (savedMicIdx >= 0) micBox.setSelectedItemIndex (savedMicIdx);
        else if (micBox.getNumItems() > 0) micBox.setSelectedItemIndex (0);

        if (savedOutIdx >= 0) outBox.setSelectedItemIndex (savedOutIdx);
        else if (outBox.getNumItems() > 0) outBox.setSelectedItemIndex (0);

        if (savedMonIdx >= 0) monBox.setSelectedItemIndex (savedMonIdx);
        else if (monBox.getNumItems() > 1) monBox.setSelectedItemIndex (1);

        applyDeviceSelection();
        monitorOut.setDevice (monBox.getText());
    }

    void buildTopBar()
    {
        searchBox.setTextToShowWhenEmpty ("Search sounds...", palette().textDim);
        addAndMakeVisible (searchBox);
        searchBox.onTextChange = [this] { filterBoard (searchBox.getText()); };

        addAndMakeVisible (stopAllSoundsButton);
        stopAllSoundsButton.getProperties().set ("danger", true);
        stopAllSoundsButton.onClick = [this] { soundboard.stopAll(); };

        addAndMakeVisible (importButton);
        importButton.onClick = [this] { showImportMenu(); };
        importButton.setVisible (false); // only shown while the Board tab is active

        addAndMakeVisible (fxRackTabButton);
        addAndMakeVisible (boardTabButton);
        addAndMakeVisible (settingsTabButton);
        addAndMakeVisible (visualizerTabButton);
        addAndMakeVisible (modsTabButton);
        addAndMakeVisible (synthTabButton);
        addAndMakeVisible (themeCreatorTabButton);
        addAndMakeVisible (ttsTabButton);
        fxRackTabButton.onClick   = [this] { showTab (Tab::FxRack); };
        boardTabButton.onClick    = [this] { showTab (Tab::Board); };
        settingsTabButton.onClick = [this] { showTab (Tab::Settings); };
        visualizerTabButton.onClick = [this] { showTab (Tab::Visualizer); };
        modsTabButton.onClick = [this] { showTab (Tab::Mods); };
        synthTabButton.onClick = [this] { showTab (Tab::Synth); };
        themeCreatorTabButton.onClick = [this] { showTab (Tab::ThemeCreator); };
        ttsTabButton.onClick = [this] { showTab (Tab::Tts); };
        visualizerTabButton.setVisible (false); // only shown in Advanced Mode
        themeCreatorTabButton.setVisible (false); // Theme Creator is Advanced Mode only

        fxRackTabButton.setVisible (true);
        boardTabButton.setVisible (true);
        modsTabButton.setVisible (true);
        synthTabButton.setVisible (true);
        ttsTabButton.setVisible (true);
    }

    void buildBoardTab()
    {
        addAndMakeVisible (boardSettingsLabel);
        boardSettingsLabel.setFont (juce::Font (12.0f));
        boardSettingsLabel.setColour (juce::Label::textColourId, palette().textDim);

        addAndMakeVisible (boardVolumeLabel);
        addAndMakeVisible (boardSpeedLabel);
        boardVolumeLabel.setFont (juce::Font (11.0f));
        boardSpeedLabel.setFont (juce::Font (11.0f));
        boardVolumeLabel.setColour (juce::Label::textColourId, palette().textDim);
        boardSpeedLabel.setColour (juce::Label::textColourId, palette().textDim);

        addAndMakeVisible (boardVolumeSlider);
        boardVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        boardVolumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        boardVolumeSlider.setRange (0.0, 2.0, 0.01);
        boardVolumeSlider.setValue (sbDefaultVolume, juce::dontSendNotification);
        boardVolumeSlider.onValueChange = [this] { sbDefaultVolume = (float) boardVolumeSlider.getValue(); };

        addAndMakeVisible (boardSpeedSlider);
        boardSpeedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        boardSpeedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        boardSpeedSlider.setRange (0.25, 3.0, 0.01);
        boardSpeedSlider.setValue (sbDefaultSpeed, juce::dontSendNotification);
        boardSpeedSlider.onValueChange = [this] { sbDefaultSpeed = (float) boardSpeedSlider.getValue(); };

        addAndMakeVisible (boardLoopToggle);
        boardLoopToggle.setToggleState (sbDefaultLoop, juce::dontSendNotification);
        boardLoopToggle.onClick = [this] { sbDefaultLoop = boardLoopToggle.getToggleState(); };

        addAndMakeVisible (boardReverseToggle);
        boardReverseToggle.setToggleState (sbDefaultReverse, juce::dontSendNotification);
        boardReverseToggle.onClick = [this] { sbDefaultReverse = boardReverseToggle.getToggleState(); };

        addAndMakeVisible (boardSelectorBar);
        boardSelectorBar.onSelect = [this] (int idx) { switchBoard (idx); };
        boardSelectorBar.onAdd    = [this] { promptAddBoard(); };
        boardSelectorBar.onRename = [this] (int idx, const juce::String& name)
        {
            soundboard.setBoardName (idx, name.toStdString());
            boardSelectorBar.refresh (soundboard);
            if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
        };
        boardSelectorBar.onChangeIcon = [this] (int idx)
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Choose a picture for this board",
                juce::File(), "*.png;*.jpg;*.jpeg;*.bmp;*.gif");
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, idx] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    soundboard.setBoardIconPath (idx, f.getFullPathName().toStdString());
                    boardSelectorBar.refresh (soundboard);
                    if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
                });
        };
        boardSelectorBar.onDelete = [this] (int idx)
        {
            const auto name = juce::String (soundboard.getBoardName (idx));
            juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Delete Board",
                "Delete \"" + name + "\" and everything on it? This can't be undone.",
                "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, idx] (int result)
                {
                    if (result != 1) return;
                    if (soundboard.deleteBoard (idx))
                    {
                        refreshCardsFromActiveBoard();
                        filterBoard (searchBox.getText());
                        boardSelectorBar.refresh (soundboard);
                        if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
                    }
                }));
        };
        boardSelectorBar.refresh (soundboard);

        boardContent.setInterceptsMouseClicks (false, true);
        for (int i = 0; i < kVisibleSlots; ++i)
        {
            auto card = std::make_unique<SoundSlotCard> (i);
            card->setSlot (&soundboard.getSlots()[(size_t) i]);
            card->onPlay = [this, i] { soundboard.play (i, sbDefaultSpeed, sbDefaultVolume, sbDefaultLoop, sbDefaultReverse); };
            card->onStop = [this, i] { soundboard.stopSlot (i); };
            card->onLoad = [this, i] { loadIntoSlot (i); };
            card->onBind = [this, i] { beginHotkeyBind (i); };
            card->onUnbind = [this, i] { unbindHotkey (i); };
            card->onDelete = [this, i]
            {
                auto& slot = soundboard.getSlots()[(size_t) i];
                const auto oldPath = slot.path;
                slot.data = nullptr;
                slot.name = "Empty";
                slot.path.clear();
                if (! oldPath.empty() && juce::File (oldPath).getParentDirectory() == recFolder)
                {
                    soundboard.ignoreAutoLoad (oldPath);
                    soundboard.saveIgnoredAutoLoad (deletedRecordingsFile);
                }
                cards[(size_t) i]->refreshFromSlot();
                if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
            };
            card->onColourPicked = [this, i] (juce::Colour c) { soundboard.getSlots()[(size_t) i].colour = c;
                                                                  soundboard.getSlots()[(size_t) i].hasColour = true;
                                                                  if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested(); };
            card->onSeek = [this, i] (float fraction) { soundboard.seekSlot (i, fraction); };
            card->onSetPlayMode = [this, i] (vlab::PlayMode mode)
            {
                soundboard.getSlots()[(size_t) i].playMode = mode;
                if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
            };
            boardContent.addAndMakeVisible (*card);
            cards[(size_t) i] = std::move (card);
        }
    }

    void refreshCardsFromActiveBoard()
    {
        auto& activeSlots = soundboard.getSlots();
        for (int i = 0; i < kVisibleSlots; ++i)
            if (cards[(size_t) i]) cards[(size_t) i]->setSlot (&activeSlots[(size_t) i]);
    }

    void switchBoard (int idx)
    {
        soundboard.setActiveBoard (idx);
        refreshCardsFromActiveBoard();
        filterBoard (searchBox.getText()); // keep the search filter consistent with the newly-shown board
        boardSelectorBar.refresh (soundboard);
        if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
    }

    // ── FX Rack presets (fxPresets save section) ────────────────────────────
    void refreshFxPresetCombo()
    {
        auto presets = vlab::loadFxPresetsFile (fxPresetsFile);
        fxPresetNames.clear();
        for (auto& prop : presets->getProperties())
            fxPresetNames.add (prop.name.toString());
        fxPresetNames.sort (true); // case-insensitive alphabetical

        fxPresetCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < fxPresetNames.size(); ++i)
            fxPresetCombo.addItem (fxPresetNames[i], i + 1);
        fxPresetCombo.setSelectedItemIndex (-1, juce::dontSendNotification);
    }

    void applyFxPreset (const juce::String& name)
    {
        auto presets = vlab::loadFxPresetsFile (fxPresetsFile);
        auto presetVar = presets->getProperty (name);
        if (! presetVar.isObject())
            return;
        vlab::fxParamsFromVar (engine.params, presetVar);
        if (refreshFxSlidersFromParams) refreshFxSlidersFromParams();
    }

    void promptSaveFxPreset()
    {
        auto* aw = new juce::AlertWindow ("Save FX Preset", "Name this preset:", juce::AlertWindow::NoIcon);
        aw->addTextEditor ("name", fxPresetCombo.getText());
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw] (int result)
            {
                if (result == 1)
                {
                    const auto name = aw->getTextEditorContents ("name").trim();
                    if (name.isNotEmpty())
                    {
                        auto presets = vlab::loadFxPresetsFile (fxPresetsFile);
                        presets->setProperty (name, vlab::fxParamsToVar (engine.params));
                        vlab::saveFxPresetsFile (fxPresetsFile, presets);
                        saveStore.commit ("fxPresets");
                        refreshFxPresetCombo();
                        fxPresetCombo.setSelectedItemIndex (fxPresetNames.indexOf (name), juce::dontSendNotification);
                    }
                }
                delete aw;
            }), false);
    }

    void deleteFxPreset (const juce::String& name)
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, "Delete Preset",
            "Delete the FX preset \"" + name + "\"? This can't be undone.", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, name] (int result)
            {
                if (result != 1)
                    return;
                auto presets = vlab::loadFxPresetsFile (fxPresetsFile);
                presets->removeProperty (name);
                vlab::saveFxPresetsFile (fxPresetsFile, presets);
                saveStore.commit ("fxPresets");
                refreshFxPresetCombo();
            }));
    }

    void promptAddBoard()
    {
        auto* aw = new juce::AlertWindow ("New Soundboard", "Name your new soundboard:", juce::AlertWindow::NoIcon);
        aw->addTextEditor ("name", "Soundboard " + juce::String (soundboard.getBoardCount() + 1));
        aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw] (int result)
            {
                if (result == 1)
                {
                    const auto name = aw->getTextEditorContents ("name").trim();
                    const int idx = soundboard.addBoard (name.toStdString());
                    switchBoard (idx);
                }
                delete aw;
            }), false);
    }

    void showUpdateDialog (const vlab::UpdateInfo& info)
    {
        if (info.mandatory)
        {
            showMandatoryUpdateBlocker (info);
            return;
        }

        juce::String message = "A new version of vlab is available: v" + info.latestVersion + ".";
        if (info.releaseNotes.isNotEmpty())
            message << "\n\nWhat's new:\n" << info.releaseNotes;
        message << "\n\nWould you like to update now?";

        auto* aw = new juce::AlertWindow ("Update Available", message, juce::AlertWindow::InfoIcon);
        aw->addButton ("Update Now", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Later", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [aw, info] (int result)
            {
                if (result == 1)
                    juce::URL (info.downloadUrl).launchInDefaultBrowser();
                delete aw;
            }), false);
    }

    void showMandatoryUpdateBlocker (const vlab::UpdateInfo& info)
    {
        juce::String message = "A required update is available: v" + info.latestVersion + ".";
        if (info.releaseNotes.isNotEmpty())
            message << "\n\nWhat's new:\n" << info.releaseNotes;
        message << "\n\nThis update is mandatory, vlab can't be used until you install it.";

        auto* aw = new juce::AlertWindow ("Update Required", message, juce::AlertWindow::WarningIcon);
        aw->addButton ("Download Update", 1, juce::KeyPress (juce::KeyPress::returnKey));
        // Deliberately no second button and no escape-key binding.
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [aw, info] (int)
            {
                juce::URL (info.downloadUrl).launchInDefaultBrowser();
                delete aw;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }), false);
    }



    // ── build: FX rack (real DSP params, sectioned like the reference) ────
    void buildFxRackTab()
    {
        fxRackContent.setInterceptsMouseClicks (false, true);
        auto& p = engine.params;

        fxRackContent.addAndMakeVisible (fxResetButton);
        fxResetButton.getProperties().set ("danger", true);
        fxResetButton.onClick = [this]
        {
            engine.params.resetToDefaults();
            if (refreshFxSlidersFromParams) refreshFxSlidersFromParams();
        };

        fxRackContent.addAndMakeVisible (fxPresetCombo);
        fxPresetCombo.setTextWhenNothingSelected ("Load FX Preset...");
        refreshFxPresetCombo();
        fxPresetCombo.onChange = [this]
        {
            const int idx = fxPresetCombo.getSelectedItemIndex();
            if (idx < 0 || idx >= fxPresetNames.size())
                return;
            applyFxPreset (fxPresetNames[idx]);
        };

        fxRackContent.addAndMakeVisible (fxCollapseAllButton);
        fxRackContent.addAndMakeVisible (fxExpandAllButton);
        fxCollapseAllButton.setVisible (modernUi);
        fxExpandAllButton.setVisible (modernUi);
        fxCollapseAllButton.onClick = [this] { setAllFxSectionsCollapsed (true); };
        fxExpandAllButton.onClick   = [this] { setAllFxSectionsCollapsed (false); };

        fxRackContent.addAndMakeVisible (fxSavePresetButton);
        fxSavePresetButton.onClick = [this] { promptSaveFxPreset(); };

        fxRackContent.addAndMakeVisible (fxDeletePresetButton);
        fxDeletePresetButton.getProperties().set ("danger", true);
        fxDeletePresetButton.onClick = [this]
        {
            const int idx = fxPresetCombo.getSelectedItemIndex();
            if (idx < 0 || idx >= fxPresetNames.size())
                return;
            deleteFxPreset (fxPresetNames[idx]);
        };

        auto addSection = [this] (const juce::String& title, const juce::String& glyph) -> FxSection&
        {
            auto s = std::make_unique<FxSection> (title, glyph);
            s->setCollapsible (true);
            s->onCollapsedChanged = [this] { layoutFxRackContent (fxRackContent.getWidth() > 0 ? fxRackContent.getWidth() : getWidth() - getSidebarWidth() - 36); };
            fxRackContent.addAndMakeVisible (*s);
            fxSections.push_back (std::move (s));
            return *fxSections.back();
        };

        auto& master = addSection ("Master & Gate", "M");
        auto& masterOutSlider = master.addSlider ("Master Out Gain (dB)", -24.0, 24.0, 0.0, [&p] (double v) { p.masterOutGain = (float) v; });
        auto& micGainSlider = master.addSlider ("Mic Input Gain (dB)", -24.0, 24.0, 0.0, [&p] (double v) { p.gain = (float) v; });
        auto& gateSlider = master.addSlider ("Noise Gate", 0.0, 0.5, 0.0, [&p] (double v) { p.noiseGate = (float) v; }, 3);

        auto& eq = addSection ("3-Band EQ", "E");
        auto& eqLoSlider = eq.addSlider ("Low Shelf (dB)", -24.0, 24.0, 0.0, [&p] (double v) { p.eqLo = (float) v; });
        auto& eqMidSlider = eq.addSlider ("Mid (dB)", -24.0, 24.0, 0.0, [&p] (double v) { p.eqMid = (float) v; });
        auto& eqHiSlider = eq.addSlider ("High Shelf (dB)", -24.0, 24.0, 0.0, [&p] (double v) { p.eqHi = (float) v; });

        auto& comp = addSection ("Compressor", "C");
        auto& compThreshSlider = comp.addSlider ("Threshold", 0.0, 1.0, 0.7, [&p] (double v) { p.compThreshold = (float) v; });
        auto& compRatioSlider = comp.addSlider ("Ratio", 1.0, 20.0, 4.0, [&p] (double v) { p.compRatio = (float) v; }, 1);
        auto& compAttackSlider = comp.addSlider ("Attack (s)", 0.001, 1.0, 0.01, [&p] (double v) { p.compAttack = (float) v; }, 3);
        auto& compReleaseSlider = comp.addSlider ("Release (s)", 0.01, 2.0, 0.1, [&p] (double v) { p.compRelease = (float) v; }, 2);

        auto& transform = addSection ("Transform", "T");
        auto& pitchSlider = transform.addSlider ("Pitch (semitones)", -12.0, 12.0, 0.0, [&p] (double v) { p.pitch = (float) v; }, 1);
        auto& distSlider = transform.addSlider ("Distortion", 0.0, 1.0, 0.0, [&p] (double v) { p.distortion = (float) v; });
        auto& bitcrushSlider = transform.addSlider ("Bitcrush (bits)", 0.0, 16.0, 0.0, [&p] (double v) { p.bitcrush = (float) v; }, 0);

        auto& mod = addSection ("Modulation", "M");
        auto& chorusMixSlider = mod.addSlider ("Chorus Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.chorusMix = (float) v; });
        auto& chorusRateSlider = mod.addSlider ("Chorus Rate (Hz)", 0.05, 5.0, 1.0, [&p] (double v) { p.chorusRate = (float) v; }, 2);
        auto& phaserMixSlider = mod.addSlider ("Phaser Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.phaserMix = (float) v; });
        auto& tremoloDepthSlider = mod.addSlider ("Tremolo Depth", 0.0, 1.0, 0.0, [&p] (double v) { p.tremoloDepth = (float) v; });

        auto& delayRev = addSection ("Delay & Reverb", "D");
        auto& delayMixSlider = delayRev.addSlider ("Delay Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.delayMix = (float) v; });
        auto& delayTimeSlider = delayRev.addSlider ("Delay Time (s)", 0.02, 1.5, 0.3, [&p] (double v) { p.delayTime = (float) v; }, 2);
        auto& delayFeedbackSlider = delayRev.addSlider ("Delay Feedback", 0.0, 0.95, 0.3, [&p] (double v) { p.delayFeedback = (float) v; });
        auto& reverbMixSlider = delayRev.addSlider ("Reverb Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.reverbMix = (float) v; });
        auto& reverbDecaySlider = delayRev.addSlider ("Reverb Decay", 0.0, 0.95, 0.5, [&p] (double v) { p.reverbDecay = (float) v; });

        auto& loudMic = addSection ("Loud Mic (Limiter)", "L");
        loudMic.addToggle ("Enable Loud Mic", false, [&p] (bool v) { p.loudMicBypass = v; });
        auto& limiterThreshSlider = loudMic.addSlider ("Limiter Threshold", 0.1, 1.0, 0.95, [&p] (double v) { p.limiterThreshold = (float) v; });
        auto& preLimiterSlider = loudMic.addSlider ("Pre-Limiter Gain", 0.1, 4.0, 1.0, [&p] (double v) { p.preLimiterGain = (float) v; });
        auto& postLimiterSlider = loudMic.addSlider ("Post-Limiter Gain", 0.1, 4.0, 1.0, [&p] (double v) { p.postLimiterGain = (float) v; });
        auto& headroomSlider = loudMic.addSlider ("Adaptive Headroom", 0.0, 0.3, 0.05, [&p] (double v) { p.adaptiveHeadroom = (float) v; });

        auto& glitch = addSection ("Glitch FX", "G");
        glitch.addToggle ("Enable Stutter", false, [&p] (bool v) { p.stutterEnabled = v; });
        glitch.addCombo ("Stutter Type", { "Loop", "Reverse Bounce", "Multi-Slice", "Half-Time", "Silence Gaps", "Zap" }, 0,
            [&p] (int idx) { p.stutterType = idx; });
        auto& stutterMixSlider = glitch.addSlider ("Stutter Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.stutterMix = (float) v; });
        auto& stutterHoldSlider = glitch.addSlider ("Stutter Hold (ms)", 20.0, 500.0, 120.0, [&p] (double v) { p.stutterHoldMs = (float) v; }, 0);
        auto& stutterProbSlider = glitch.addSlider ("Stutter Rate (triggers/s)", 0.0, 5.0, 1.0, [&p] (double v) { p.stutterRate = (float) v; }, 1);
        auto& stutterRandomSlider = glitch.addSlider ("Stutter Randomness", 0.0, 1.0, 0.5, [&p] (double v) { p.stutterRandomness = (float) v; });
        auto& ringModMixSlider = glitch.addSlider ("Ring Mod Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.ringModMix = (float) v; });
        auto& ringModFreqSlider = glitch.addSlider ("Ring Mod Freq (Hz)", 1.0, 800.0, 30.0, [&p] (double v) { p.ringModFreq = (float) v; }, 0);

        glitch.addToggle ("Escalating Repeat (hold to run)", false, [&p] (bool v) { p.escRepeatEnabled = v; });
        auto& escRepeatMixSlider = glitch.addSlider ("Escalating Repeat Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.escRepeatMix = (float) v; });
        auto& escRepeatHoldSlider = glitch.addSlider ("Escalating Repeat Hold (ms)", 20.0, 500.0, 150.0, [&p] (double v) { p.escRepeatHoldMs = (float) v; }, 0);
        auto& escRepeatGrowthSlider = glitch.addSlider ("Escalating Repeat Loudness Speed", 1.0, 2.0, 1.3, [&p] (double v) { p.escRepeatGrowth = (float) v; }, 2);

        glitch.addToggle ("Accelerating Repeat (hold to run)", false, [&p] (bool v) { p.accRepeatEnabled = v; });
        auto& accRepeatMixSlider = glitch.addSlider ("Accelerating Repeat Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.accRepeatMix = (float) v; });
        auto& accRepeatHoldSlider = glitch.addSlider ("Accelerating Repeat Hold (ms)", 40.0, 600.0, 200.0, [&p] (double v) { p.accRepeatHoldMs = (float) v; }, 0);
        auto& accRepeatSpeedSlider = glitch.addSlider ("Accelerating Repeat Speed-Up Rate", 1.0, 2.0, 1.3, [&p] (double v) { p.accRepeatSpeedGrowth = (float) v; }, 2);

        glitch.addToggle ("Decelerate Repeat (hold to run)", false, [&p] (bool v) { p.decRepeatEnabled = v; });
        auto& decRepeatMixSlider = glitch.addSlider ("Decelerate Repeat Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.decRepeatMix = (float) v; });
        auto& decRepeatHoldSlider = glitch.addSlider ("Decelerate Repeat Hold (ms)", 40.0, 600.0, 200.0, [&p] (double v) { p.decRepeatHoldMs = (float) v; }, 0);
        auto& decRepeatShrinkSlider = glitch.addSlider ("Decelerate Repeat Slow-Down Rate", 0.3, 0.98, 0.8, [&p] (double v) { p.decRepeatSpeedShrink = (float) v; }, 2);

        glitch.addToggle ("Repeat Pitch Down (hold to run)", false, [&p] (bool v) { p.pitchDownRepeatEnabled = v; });
        auto& pitchDownRepeatMixSlider = glitch.addSlider ("Repeat Pitch Down Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.pitchDownRepeatMix = (float) v; });
        auto& pitchDownRepeatHoldSlider = glitch.addSlider ("Repeat Pitch Down Hold (ms)", 40.0, 600.0, 200.0, [&p] (double v) { p.pitchDownRepeatHoldMs = (float) v; }, 0);
        auto& pitchDownRepeatSemitonesSlider = glitch.addSlider ("Repeat Pitch Down Semitones/loop", 0.0, 12.0, 2.0, [&p] (double v) { p.pitchDownRepeatSemitones = (float) v; }, 1);

        glitch.addToggle ("Enable Scatter", false, [&p] (bool v) { p.scatterEnabled = v; });
        auto& scatterMixSlider = glitch.addSlider ("Scatter Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.scatterMix = (float) v; });
        auto& scatterGrainSlider = glitch.addSlider ("Scatter Grain (ms)", 15.0, 300.0, 60.0, [&p] (double v) { p.scatterGrainMs = (float) v; }, 0);

        glitch.addToggle ("Enable Reverse Bursts", false, [&p] (bool v) { p.reverseBurstEnabled = v; });
        auto& reverseBurstMixSlider = glitch.addSlider ("Reverse Burst Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.reverseBurstMix = (float) v; });
        auto& reverseBurstMsSlider = glitch.addSlider ("Reverse Burst Length (ms)", 50.0, 600.0, 200.0, [&p] (double v) { p.reverseBurstMs = (float) v; }, 0);
        auto& reverseBurstProbSlider = glitch.addSlider ("Reverse Burst Rate (triggers/s)", 0.0, 3.0, 0.5, [&p] (double v) { p.reverseBurstProbability = (float) v; }, 1);

        glitch.addToggle ("Enable Glitch Gate", false, [&p] (bool v) { p.glitchGateEnabled = v; });
        auto& glitchGateMixSlider = glitch.addSlider ("Glitch Gate Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.glitchGateMix = (float) v; });
        auto& glitchGateStepSlider = glitch.addSlider ("Glitch Gate Step (ms)", 20.0, 400.0, 80.0, [&p] (double v) { p.glitchGateStepMs = (float) v; }, 0);
        auto& glitchGateDensitySlider = glitch.addSlider ("Glitch Gate Density", 0.0, 1.0, 0.4, [&p] (double v) { p.glitchGateDensity = (float) v; });

        glitch.addToggle ("Enable Buffer Freeze", false, [&p] (bool v) { p.freezeEnabled = v; });
        auto& freezeMixSlider = glitch.addSlider ("Buffer Freeze Mix", 0.0, 1.0, 1.0, [&p] (double v) { p.freezeMix = (float) v; });
        auto& freezeGrainSlider = glitch.addSlider ("Buffer Freeze Grain (ms)", 30.0, 500.0, 150.0, [&p] (double v) { p.freezeGrainMs = (float) v; }, 0);

        glitch.addToggle ("Enable Vinyl Stop", false, [&p] (bool v) { p.vinylStopEnabled = v; });
        auto& vinylStopMixSlider = glitch.addSlider ("Vinyl Stop Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.vinylStopMix = (float) v; });
        auto& vinylStopMsSlider = glitch.addSlider ("Vinyl Stop Length (ms)", 100.0, 2000.0, 500.0, [&p] (double v) { p.vinylStopMs = (float) v; }, 0);
        auto& vinylStopProbSlider = glitch.addSlider ("Vinyl Stop Rate (triggers/s)", 0.0, 2.0, 0.3, [&p] (double v) { p.vinylStopProbability = (float) v; }, 1);

        glitch.addToggle ("Enable Broken Delay", false, [&p] (bool v) { p.brokenDelayEnabled = v; });
        auto& brokenDelayMixSlider = glitch.addSlider ("Broken Delay Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.brokenDelayMix = (float) v; });
        auto& brokenDelayMsSlider = glitch.addSlider ("Broken Delay Time (ms)", 20.0, 1500.0, 300.0, [&p] (double v) { p.brokenDelayMs = (float) v; }, 0);
        auto& brokenDelayFeedbackSlider = glitch.addSlider ("Broken Delay Feedback", 0.0, 0.95, 0.35, [&p] (double v) { p.brokenDelayFeedback = (float) v; });
        auto& brokenDelayJumpSlider = glitch.addSlider ("Broken Delay Jump Rate", 0.0, 5.0, 1.0, [&p] (double v) { p.brokenDelayJumpRate = (float) v; }, 1);

        glitch.addToggle ("Enable Pitch Jumps", false, [&p] (bool v) { p.pitchJumpEnabled = v; });
        auto& pitchJumpMixSlider = glitch.addSlider ("Pitch Jumps Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.pitchJumpMix = (float) v; });
        auto& pitchJumpMsSlider = glitch.addSlider ("Pitch Jump Length (ms)", 30.0, 800.0, 150.0, [&p] (double v) { p.pitchJumpMs = (float) v; }, 0);
        auto& pitchJumpSemitonesSlider = glitch.addSlider ("Pitch Jump Range (semitones)", 1.0, 24.0, 12.0, [&p] (double v) { p.pitchJumpSemitones = (float) v; }, 0);
        auto& pitchJumpRateSlider = glitch.addSlider ("Pitch Jump Rate (triggers/s)", 0.0, 5.0, 1.0, [&p] (double v) { p.pitchJumpRate = (float) v; }, 1);

        glitch.addToggle ("Enable Backmask", false, [&p] (bool v) { p.backmaskEnabled = v; });
        auto& backmaskMixSlider = glitch.addSlider ("Backmask Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.backmaskMix = (float) v; });
        auto& backmaskSegmentSlider = glitch.addSlider ("Backmask Segment (ms)", 40.0, 800.0, 200.0, [&p] (double v) { p.backmaskSegmentMs = (float) v; }, 0);

        glitch.addToggle ("Enable Micro Freeze", false, [&p] (bool v) { p.microFreezeEnabled = v; });
        auto& microFreezeMixSlider = glitch.addSlider ("Micro Freeze Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.microFreezeMix = (float) v; });
        auto& microFreezeHoldSlider = glitch.addSlider ("Micro Freeze Hold (ms)", 1.0, 40.0, 8.0, [&p] (double v) { p.microFreezeHoldMs = (float) v; }, 1);
        auto& microFreezeDensitySlider = glitch.addSlider ("Micro Freeze Density", 0.0, 1.0, 0.4, [&p] (double v) { p.microFreezeDensity = (float) v; });

        glitch.addToggle ("Enable Buffer Shuffle", false, [&p] (bool v) { p.bufferShuffleEnabled = v; });
        auto& bufferShuffleMixSlider = glitch.addSlider ("Buffer Shuffle Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.bufferShuffleMix = (float) v; });
        auto& bufferShuffleHoldSlider = glitch.addSlider ("Buffer Shuffle Window (ms)", 150.0, 2000.0, 500.0, [&p] (double v) { p.bufferShuffleHoldMs = (float) v; }, 0);
        auto& bufferShuffleChunksSlider = glitch.addSlider ("Buffer Shuffle Chunks", 2.0, 16.0, 8.0, [&p] (double v) { p.bufferShuffleChunks = (int) v; }, 0);

        auto& experimental = addSection ("Experimental Voice FX", "!");
        experimental.addToggle ("Enable De-Esser", false, [&p] (bool v) { p.expDeEsserEnabled = v; });
        auto& deEsserSlider = experimental.addSlider ("De-Esser Amount", 0.0, 1.0, 0.5, [&p] (double v) { p.expDeEsserAmount = (float) v; });

        experimental.addToggle ("Enable Auto-Level", false, [&p] (bool v) { p.expAutoLevelEnabled = v; });
        auto& autoLevelSlider = experimental.addSlider ("Auto-Level Amount", 0.0, 1.0, 0.7, [&p] (double v) { p.expAutoLevelAmount = (float) v; });

        experimental.addToggle ("Enable Underwater", false, [&p] (bool v) { p.expUnderwaterEnabled = v; });
        auto& underwaterSlider = experimental.addSlider ("Underwater Amount", 0.0, 1.0, 0.7, [&p] (double v) { p.expUnderwaterAmount = (float) v; });

        experimental.addToggle ("Enable Presence Boost", false, [&p] (bool v) { p.expPresenceEnabled = v; });
        auto& presenceSlider = experimental.addSlider ("Presence Boost Amount", 0.0, 1.0, 0.5, [&p] (double v) { p.expPresenceAmount = (float) v; });

        experimental.addToggle ("Enable Radio Sweep", false, [&p] (bool v) { p.expRadioSweepEnabled = v; });
        auto& radioSweepAmountSlider = experimental.addSlider ("Radio Sweep Amount", 0.0, 1.0, 0.7, [&p] (double v) { p.expRadioSweepAmount = (float) v; });
        auto& radioSweepRateSlider = experimental.addSlider ("Radio Sweep Rate (Hz)", 0.05, 2.0, 0.3, [&p] (double v) { p.expRadioSweepRate = (float) v; }, 2);

        experimental.addToggle ("Enable Pump", false, [&p] (bool v) { p.expPumpEnabled = v; });
        auto& pumpDepthSlider = experimental.addSlider ("Pump Depth", 0.0, 1.0, 0.6, [&p] (double v) { p.expPumpDepth = (float) v; });
        auto& pumpRateSlider = experimental.addSlider ("Pump Rate (Hz)", 0.2, 8.0, 2.0, [&p] (double v) { p.expPumpRate = (float) v; }, 1);

        auto& vanilla = addSection ("Vanilla FX", "V");
        auto& flangerMixSlider = vanilla.addSlider ("Flanger Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.flangerMix = (float) v; });
        auto& flangerRateSlider = vanilla.addSlider ("Flanger Rate (Hz)", 0.05, 3.0, 0.3, [&p] (double v) { p.flangerRate = (float) v; }, 2);
        auto& flangerFeedbackSlider = vanilla.addSlider ("Flanger Feedback", 0.0, 0.9, 0.3, [&p] (double v) { p.flangerFeedback = (float) v; });
        auto& autoWahMixSlider = vanilla.addSlider ("Auto-Wah Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.autoWahMix = (float) v; });
        auto& autoWahSensSlider = vanilla.addSlider ("Auto-Wah Sensitivity", 0.0, 2.0, 1.0, [&p] (double v) { p.autoWahSensitivity = (float) v; });
        auto& vibratoDepthSlider = vanilla.addSlider ("Vibrato Depth", 0.0, 1.0, 0.0, [&p] (double v) { p.vibratoDepth = (float) v; });
        auto& vibratoRateSlider = vanilla.addSlider ("Vibrato Rate (Hz)", 1.0, 12.0, 5.0, [&p] (double v) { p.vibratoRate = (float) v; }, 1);
        auto& autoPanDepthSlider = vanilla.addSlider ("Auto-Pan Depth", 0.0, 1.0, 0.0, [&p] (double v) { p.autoPanDepth = (float) v; });
        auto& autoPanRateSlider = vanilla.addSlider ("Auto-Pan Rate (Hz)", 0.05, 4.0, 0.5, [&p] (double v) { p.autoPanRate = (float) v; }, 2);
        auto& telephoneMixSlider = vanilla.addSlider ("Telephone Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.telephoneMix = (float) v; });
        auto& doublerMixSlider = vanilla.addSlider ("Doubler Mix", 0.0, 1.0, 0.0, [&p] (double v) { p.doublerMix = (float) v; });
        auto& doublerDetuneSlider = vanilla.addSlider ("Doubler Detune (cents)", 0.0, 40.0, 12.0, [&p] (double v) { p.doublerDetuneCents = (float) v; }, 0);
        auto& doublerDelaySlider = vanilla.addSlider ("Doubler Delay (ms)", 5.0, 40.0, 18.0, [&p] (double v) { p.doublerDelayMs = (float) v; }, 0);

        // Sync sliders when params change programmatically
        refreshFxSlidersFromParams = [&, this]
        {
            masterOutSlider.setValue (p.masterOutGain, juce::sendNotificationSync);
            micGainSlider.setValue (p.gain, juce::sendNotificationSync);
            gateSlider.setValue (p.noiseGate, juce::sendNotificationSync);
            eqLoSlider.setValue (p.eqLo, juce::sendNotificationSync);
            eqMidSlider.setValue (p.eqMid, juce::sendNotificationSync);
            eqHiSlider.setValue (p.eqHi, juce::sendNotificationSync);
            compThreshSlider.setValue (p.compThreshold, juce::sendNotificationSync);
            compRatioSlider.setValue (p.compRatio, juce::sendNotificationSync);
            compAttackSlider.setValue (p.compAttack, juce::sendNotificationSync);
            compReleaseSlider.setValue (p.compRelease, juce::sendNotificationSync);
            pitchSlider.setValue (p.pitch, juce::sendNotificationSync);
            distSlider.setValue (p.distortion, juce::sendNotificationSync);
            bitcrushSlider.setValue (p.bitcrush, juce::sendNotificationSync);
            chorusMixSlider.setValue (p.chorusMix, juce::sendNotificationSync);
            chorusRateSlider.setValue (p.chorusRate, juce::sendNotificationSync);
            phaserMixSlider.setValue (p.phaserMix, juce::sendNotificationSync);
            tremoloDepthSlider.setValue (p.tremoloDepth, juce::sendNotificationSync);
            delayMixSlider.setValue (p.delayMix, juce::sendNotificationSync);
            delayTimeSlider.setValue (p.delayTime, juce::sendNotificationSync);
            delayFeedbackSlider.setValue (p.delayFeedback, juce::sendNotificationSync);
            reverbMixSlider.setValue (p.reverbMix, juce::sendNotificationSync);
            reverbDecaySlider.setValue (p.reverbDecay, juce::sendNotificationSync);
            limiterThreshSlider.setValue (p.limiterThreshold, juce::sendNotificationSync);
            preLimiterSlider.setValue (p.preLimiterGain, juce::sendNotificationSync);
            postLimiterSlider.setValue (p.postLimiterGain, juce::sendNotificationSync);
            headroomSlider.setValue (p.adaptiveHeadroom, juce::sendNotificationSync);
            stutterMixSlider.setValue (p.stutterMix, juce::sendNotificationSync);
            stutterHoldSlider.setValue (p.stutterHoldMs, juce::sendNotificationSync);
            stutterProbSlider.setValue (p.stutterRate, juce::sendNotificationSync);
            stutterRandomSlider.setValue (p.stutterRandomness, juce::sendNotificationSync);
            ringModMixSlider.setValue (p.ringModMix, juce::sendNotificationSync);
            ringModFreqSlider.setValue (p.ringModFreq, juce::sendNotificationSync);
            escRepeatMixSlider.setValue (p.escRepeatMix, juce::sendNotificationSync);
            escRepeatHoldSlider.setValue (p.escRepeatHoldMs, juce::sendNotificationSync);
            escRepeatGrowthSlider.setValue (p.escRepeatGrowth, juce::sendNotificationSync);
            accRepeatMixSlider.setValue (p.accRepeatMix, juce::sendNotificationSync);
            accRepeatHoldSlider.setValue (p.accRepeatHoldMs, juce::sendNotificationSync);
            accRepeatSpeedSlider.setValue (p.accRepeatSpeedGrowth, juce::sendNotificationSync);
            decRepeatMixSlider.setValue (p.decRepeatMix, juce::sendNotificationSync);
            decRepeatHoldSlider.setValue (p.decRepeatHoldMs, juce::sendNotificationSync);
            decRepeatShrinkSlider.setValue (p.decRepeatSpeedShrink, juce::sendNotificationSync);
            pitchDownRepeatMixSlider.setValue (p.pitchDownRepeatMix, juce::sendNotificationSync);
            pitchDownRepeatHoldSlider.setValue (p.pitchDownRepeatHoldMs, juce::sendNotificationSync);
            pitchDownRepeatSemitonesSlider.setValue (p.pitchDownRepeatSemitones, juce::sendNotificationSync);
            scatterMixSlider.setValue (p.scatterMix, juce::sendNotificationSync);
            scatterGrainSlider.setValue (p.scatterGrainMs, juce::sendNotificationSync);
            reverseBurstMixSlider.setValue (p.reverseBurstMix, juce::sendNotificationSync);
            reverseBurstMsSlider.setValue (p.reverseBurstMs, juce::sendNotificationSync);
            reverseBurstProbSlider.setValue (p.reverseBurstProbability, juce::sendNotificationSync);
            glitchGateMixSlider.setValue (p.glitchGateMix, juce::sendNotificationSync);
            glitchGateStepSlider.setValue (p.glitchGateStepMs, juce::sendNotificationSync);
            glitchGateDensitySlider.setValue (p.glitchGateDensity, juce::sendNotificationSync);
            freezeMixSlider.setValue (p.freezeMix, juce::sendNotificationSync);
            freezeGrainSlider.setValue (p.freezeGrainMs, juce::sendNotificationSync);
            vinylStopMixSlider.setValue (p.vinylStopMix, juce::sendNotificationSync);
            vinylStopMsSlider.setValue (p.vinylStopMs, juce::sendNotificationSync);
            vinylStopProbSlider.setValue (p.vinylStopProbability, juce::sendNotificationSync);
            brokenDelayMixSlider.setValue (p.brokenDelayMix, juce::sendNotificationSync);
            brokenDelayMsSlider.setValue (p.brokenDelayMs, juce::sendNotificationSync);
            brokenDelayFeedbackSlider.setValue (p.brokenDelayFeedback, juce::sendNotificationSync);
            brokenDelayJumpSlider.setValue (p.brokenDelayJumpRate, juce::sendNotificationSync);
            pitchJumpMixSlider.setValue (p.pitchJumpMix, juce::sendNotificationSync);
            pitchJumpMsSlider.setValue (p.pitchJumpMs, juce::sendNotificationSync);
            pitchJumpSemitonesSlider.setValue (p.pitchJumpSemitones, juce::sendNotificationSync);
            pitchJumpRateSlider.setValue (p.pitchJumpRate, juce::sendNotificationSync);
            backmaskMixSlider.setValue (p.backmaskMix, juce::sendNotificationSync);
            backmaskSegmentSlider.setValue (p.backmaskSegmentMs, juce::sendNotificationSync);
            microFreezeMixSlider.setValue (p.microFreezeMix, juce::sendNotificationSync);
            microFreezeHoldSlider.setValue (p.microFreezeHoldMs, juce::sendNotificationSync);
            microFreezeDensitySlider.setValue (p.microFreezeDensity, juce::sendNotificationSync);
            bufferShuffleMixSlider.setValue (p.bufferShuffleMix, juce::sendNotificationSync);
            bufferShuffleHoldSlider.setValue (p.bufferShuffleHoldMs, juce::sendNotificationSync);
            bufferShuffleChunksSlider.setValue (p.bufferShuffleChunks, juce::sendNotificationSync);
            deEsserSlider.setValue (p.expDeEsserAmount, juce::sendNotificationSync);
            autoLevelSlider.setValue (p.expAutoLevelAmount, juce::sendNotificationSync);
            underwaterSlider.setValue (p.expUnderwaterAmount, juce::sendNotificationSync);
            presenceSlider.setValue (p.expPresenceAmount, juce::sendNotificationSync);
            radioSweepAmountSlider.setValue (p.expRadioSweepAmount, juce::sendNotificationSync);
            radioSweepRateSlider.setValue (p.expRadioSweepRate, juce::sendNotificationSync);
            pumpDepthSlider.setValue (p.expPumpDepth, juce::sendNotificationSync);
            pumpRateSlider.setValue (p.expPumpRate, juce::sendNotificationSync);
            flangerMixSlider.setValue (p.flangerMix, juce::sendNotificationSync);
            flangerRateSlider.setValue (p.flangerRate, juce::sendNotificationSync);
            flangerFeedbackSlider.setValue (p.flangerFeedback, juce::sendNotificationSync);
            autoWahMixSlider.setValue (p.autoWahMix, juce::sendNotificationSync);
            autoWahSensSlider.setValue (p.autoWahSensitivity, juce::sendNotificationSync);
            vibratoDepthSlider.setValue (p.vibratoDepth, juce::sendNotificationSync);
            vibratoRateSlider.setValue (p.vibratoRate, juce::sendNotificationSync);
            autoPanDepthSlider.setValue (p.autoPanDepth, juce::sendNotificationSync);
            autoPanRateSlider.setValue (p.autoPanRate, juce::sendNotificationSync);
            telephoneMixSlider.setValue (p.telephoneMix, juce::sendNotificationSync);
            doublerMixSlider.setValue (p.doublerMix, juce::sendNotificationSync);
            doublerDetuneSlider.setValue (p.doublerDetuneCents, juce::sendNotificationSync);
            doublerDelaySlider.setValue (p.doublerDelayMs, juce::sendNotificationSync);
        };
    }

    void speakTtsInput()
    {
        tts.speak (ttsInput.getText());
        ttsInput.clear();
    }

    // ── build: Text-to-Speech tab ────────────────────────────────────────
    void buildTtsTab()
    {
        ttsContent.setInterceptsMouseClicks (false, true);

        auto addSection = [this] (const juce::String& title, const juce::String& glyph) -> FxSection&
        {
            auto s = std::make_unique<FxSection> (title, glyph);
            ttsContent.addAndMakeVisible (*s);
            ttsSections.push_back (std::move (s));
            return *ttsSections.back();
        };

        auto& voiceSection = addSection ("Voice", "V");
        juce::StringArray ttsVoiceNames;
        ttsVoiceNames.add ("Default (system)");
        ttsVoiceNames.addArray (vlab::LiveTts::getVoices());
        voiceSection.addCombo ("Voice", ttsVoiceNames, 0, [this, ttsVoiceNames] (int idx)
        {
            tts.voiceId = (idx <= 0) ? juce::String() : ttsVoiceNames[idx];
        });

        auto& soundSection = addSection ("Voice Controls", "P");
        soundSection.addSlider ("Speed (words/min)", 80.0, 400.0, 150.0,
            [this] (double v) { tts.rate = (int) v; }, 0);
        soundSection.addSlider ("Pitch", 0.0, 99.0, 50.0,
            [this] (double v) { tts.pitch = (int) v; }, 0);
        soundSection.addSlider ("Volume", 0.0, 2.0, 1.0,
            [this] (double v) { tts.volume = (float) v; }, 2);
        soundSection.addSlider ("Pan", -1.0, 1.0, 0.0,
            [this] (double v) { tts.pan = (float) v; }, 2);

        ttsContent.addAndMakeVisible (ttsLabel);
        ttsContent.addAndMakeVisible (ttsInput);
        ttsContent.addAndMakeVisible (speakButton);
        ttsContent.addAndMakeVisible (stopTtsButton);
        ttsLabel.setColour (juce::Label::textColourId, palette().textDim);
        ttsLabel.setFont (juce::Font (12.0f));
        ttsInput.setTextToShowWhenEmpty ("Type something to speak...", palette().textDim);
        ttsInput.onReturnKey = [this] { speakTtsInput(); };
        speakButton.getProperties().set ("primary", true);
        speakButton.onClick = [this] { speakTtsInput(); };
        stopTtsButton.onClick = [this] { soundboard.clearTtsQueue(); };
    }

    // ── build: Settings tab ─────────────────────────────────────────────
    void buildSettingsTab()
    {
        settingsContent.setInterceptsMouseClicks (false, true);
        auto& p = engine.params;

        auto addSection = [this] (const juce::String& title, const juce::String& glyph) -> FxSection&
        {
            auto s = std::make_unique<FxSection> (title, glyph);
            settingsContent.addAndMakeVisible (*s);
            settingsSections.push_back (std::move (s));
            return *settingsSections.back();
        };

        auto& general = addSection ("General", "G");
        general.addToggle ("New UI (turn off for the classic layout)", modernUi, [this] (bool v)
        {
            modernUi = v;
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("enabled", modernUi);
            modernUiPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
            saveStore.commit ("modernUi");
            applyUiMode();
        });
        general.addToggle ("Advanced Mode", advancedMode, [this] (bool v)
        {
            advancedMode = v;
            visualizerTabButton.setVisible (advancedMode);
            themeCreatorTabButton.setVisible (advancedMode);
            resized();
            if (! advancedMode && currentTab == Tab::Visualizer)
                showTab (Tab::Settings);
            if (! advancedMode && currentTab == Tab::ThemeCreator)
                showTab (Tab::Settings);
            if (! advancedMode && modderView != nullptr)
                toggleModderView(); // closes it, same "advanced-only tool goes away with the setting" rule as above
        });

        general.addToggle ("Play Startup Sound", startupSoundEnabled, [this] (bool v)
        {
            startupSoundEnabled = v;
            saveStartupSoundPreference();
        });

        general.addButton ("Reload All Mods", "Reloads audio mods, visualizer mods, App Mods, themes, and language mods from disk without restarting vlab",
            [this] { reloadAllMods(); });

        general.addButton ("Modder View", "In-app element inspector, hover to see any component's class/bounds, click to pin it. Ctrl+Shift+M also toggles this. Advanced Mode only.",
            [this] { toggleModderView(); });

        settingsContent.addAndMakeVisible (startupSoundStatusLabel);
        startupSoundStatusLabel.setFont (juce::Font (11.5f));
        startupSoundStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        startupSoundStatusLabel.setText (startupJingleBuffer != nullptr
            ? ("Startup sound: " + startupSoundFile.getFileName())
            : "No startup sound file found, pick any audio file below.",
            juce::dontSendNotification);

        settingsContent.addAndMakeVisible (chooseStartupSoundButton);
        chooseStartupSoundButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Choose a startup sound",
                juce::File(), "*.wav;*.aiff;*.flac;*.ogg;*.mp3");
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    startupSoundFile = f;
                    loadStartupJingle();
                    startupSoundStatusLabel.setText (startupJingleBuffer != nullptr
                        ? ("Startup sound: " + startupSoundFile.getFileName())
                        : "Couldn't decode that file, try a different one.",
                        juce::dontSendNotification);
                    saveStartupSoundPreference();
                });
        };

        auto themeNames = themeManager.getThemeNames();
        int currentThemeIdx = juce::jmax (0, themeNames.indexOf (themeManager.getLastAppliedThemeName()));
        general.addCombo ("Theme", themeNames, currentThemeIdx, [this, themeNames] (int idx)
        {
            if (idx >= 0 && idx < themeNames.size())
            {
                themeManager.applyTheme (themeNames[idx]);
                themeManager.savePreference (themePrefFile);
                saveStore.commit ("theme");
                rgbEngine.refreshBase();
                refreshDiscordPresence();
                repaint();
                for (auto* c : { (juce::Component*) &boardContent, &fxRackContent, &settingsContent,
                                  &visualizerContent, &synthContent, &modsContent, &themeCreatorContent })
                    c->repaint();
            }
        });

        auto uiPresetNames = uiPresetManager.getPresetNames();
        int currentUiPresetIdx = juce::jmax (0, uiPresetNames.indexOf (uiPresetManager.getLastAppliedPresetName()));
        general.addCombo ("UI Preset", uiPresetNames, currentUiPresetIdx, [this, uiPresetNames] (int idx)
        {
            if (idx >= 0 && idx < uiPresetNames.size())
            {
                uiPresetManager.applyPreset (uiPresetNames[idx]);
                uiPresetManager.savePreference (uiPresetPrefFile);
                saveStore.commit ("uiPreset");
                repaint();
                for (auto* c : { (juce::Component*) &boardContent, &fxRackContent, &settingsContent,
                                  &visualizerContent, &synthContent, &modsContent, &themeCreatorContent })
                    c->repaint();
            }
        });

        general.addCombo ("Loading Screen", { "Blue", "Green", "Red", "Pink", "Gray", "Retro 95", "Purple", "Orange", "Cyan", "Amber Terminal" }, splashStyleIndex,
            [this] (int idx)
        {
            splashStyleIndex = idx;
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("style", idx);
            splashStylePrefFile.replaceWithText (juce::JSON::toString (juce::var (obj)));
            saveStore.commit ("splashStyle");
        });

        auto languageNames = translationManager.getLanguageNames();
        int currentLanguageIdx = juce::jmax (0, languageNames.indexOf (translationManager.getLastAppliedLanguageName()));
        general.addCombo ("Language", languageNames, currentLanguageIdx, [this, languageNames] (int idx)
        {
            if (idx >= 0 && idx < languageNames.size())
            {
                translationManager.applyLanguage (languageNames[idx]);
                translationManager.savePreference (languagePrefFile);
                saveStore.commit ("language");
                applyTranslations();
            }
        });

        settingsContent.addAndMakeVisible (sbFolderLabel);
        sbFolderLabel.setFont (juce::Font (11.0f));
        sbFolderLabel.setColour (juce::Label::textColourId, palette().textDim);
        sbFolderLabel.setText ("Recordings folder: " + recFolder.getFullPathName(), juce::dontSendNotification);
        settingsContent.addAndMakeVisible (openRecordingsFolderButton);
        openRecordingsFolderButton.onClick = [this] { recFolder.revealToUser(); };

        auto& background = addSection ("Background", "B");
        background.addSlider ("Background Opacity", 0.0, 1.0, (double) backgroundLayer.getOpacity(),
            [this] (double v) { backgroundLayer.setOpacity ((float) v); saveBackgroundPreference(); });

        settingsContent.addAndMakeVisible (backgroundStatusLabel);
        backgroundStatusLabel.setFont (juce::Font (11.5f));
        backgroundStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        backgroundStatusLabel.setText (backgroundLayer.hasImage()
            ? ("Background: " + backgroundLayer.getSourceName())
            : "No background set - any static image or an animated GIF works.",
            juce::dontSendNotification);

        settingsContent.addAndMakeVisible (loadBackgroundButton);
        settingsContent.addAndMakeVisible (clearBackgroundButton);
        loadBackgroundButton.getProperties().set ("primary", true);
        loadBackgroundButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Choose a background image or GIF",
                juce::File(), "*.png;*.jpg;*.jpeg;*.bmp;*.gif");
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    const auto warning = backgroundLayer.loadFile (f);
                    lastBackgroundPath = f;
                    backgroundStatusLabel.setText (
                        warning.isNotEmpty() ? warning : ("Background: " + backgroundLayer.getSourceName()),
                        juce::dontSendNotification);
                    saveBackgroundPreference();
                });
        };
        clearBackgroundButton.getProperties().set ("danger", true);
        clearBackgroundButton.onClick = [this]
        {
            backgroundLayer.clear();
            lastBackgroundPath = juce::File();
            backgroundStatusLabel.setText ("No background set.", juce::dontSendNotification);
            saveBackgroundPreference();
        };

        auto& stereo = addSection ("Stereo Mic / HeadSpace", "H");
        stereo.addToggle ("Enable HeadSpace", false, [&p] (bool v) { p.headSpaceEnabled = v; });
        auto* azSliderPtr = &stereo.addSlider ("Azimuth (deg)", -180.0, 180.0, 0.0,
            [this] (double v) { engine.params.headSpaceAzimuth = (float) v; settingsHeadWidget.setAzimuth ((float) v); }, 0);
        stereo.addSlider ("Width", 0.0, 1.0, 0.8, [&p] (double v) { p.headSpaceWidth = (float) v; });

        settingsContent.addAndMakeVisible (settingsHeadWidget);
        settingsHeadWidget.onAzimuthChanged = [this, azSliderPtr] (float deg)
        {
            engine.params.headSpaceAzimuth = deg;
            azSliderPtr->setValue (deg, juce::dontSendNotification);
        };

        // ── Recorder: record the mic input straight into a soundboard slot ──
        auto& recorder = addSection ("Recorder", "R");
        juce::StringArray slotOptions;
        slotOptions.add ("Next Empty Slot");
        for (int i = 0; i < kVisibleSlots; ++i) slotOptions.add ("Slot " + juce::String (i + 1));
        recorder.addCombo ("Target Slot", slotOptions, 0, [this] (int idx) { recordTargetSlot = idx - 1; });
        recordToggleBtn = &recorder.addToggle ("Recording", false, [this] (bool v) { setRecordingActive (v); });

        settingsContent.addAndMakeVisible (setRecordHotkeyButton);
        setRecordHotkeyButton.setTooltip ("Single-tap to bind, double-tap to remove the current record hotkey");
        setRecordHotkeyButton.onClick = [this]
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (lastRecordHotkeyBtnClickMs != 0 && now - lastRecordHotkeyBtnClickMs < 400)
            {
                lastRecordHotkeyBtnClickMs = 0;
                unbindRecordHotkey();
            }
            else
            {
                lastRecordHotkeyBtnClickMs = now;
                beginRecordHotkeyBind();
            }
        };

        settingsContent.addAndMakeVisible (recorderStatusLabel);
        recorderStatusLabel.setFont (juce::Font (11.5f));
        recorderStatusLabel.setJustificationType (juce::Justification::topLeft);
        recorderStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        recorderStatusLabel.setText (recordHotkey.isNotEmpty()
            ? ("Record hotkey: " + recordHotkey.toUpperCase() + ". Click a soundboard card's keyboard icon to bind a slot hotkey.")
            : "No record hotkey set yet. Click \"Set Record Hotkey\" and press a key, or use the toggle above. "
              "Click a soundboard card's keyboard icon to bind a slot hotkey.",
            juce::dontSendNotification);

        // ── Experimental Features (opt-in, clearly flagged) ──
        settingsContent.addAndMakeVisible (experimentalWarningLabel);
        experimentalWarningLabel.setText (
            "Warning: Experimental, these are unfinished/unproven features. They may crash the app, "
            "break audio routing, or simply not work on your machine. Off by default; nothing here is "
            "needed for normal use. Enable at your own risk.",
            juce::dontSendNotification);
        experimentalWarningLabel.setFont (juce::Font (12.0f, juce::Font::bold));
        experimentalWarningLabel.setColour (juce::Label::textColourId, palette().danger);
        experimentalWarningLabel.setJustificationType (juce::Justification::topLeft);

        auto& experimental = addSection ("Experimental Features", "!");
        experimental.addToggle ("Enable Experimental Features", experimentalEnabled,
            [this] (bool v) { experimentalEnabled = v; applyAllExperimental(); saveExperimentalPreference(); });
        experimentalUncappedRgbToggle = &experimental.addToggle ("Uncapped RGB Framerate (smoother, more CPU)", experimentalUncappedRgb,
            [this] (bool v) { experimentalUncappedRgb = v; applyAllExperimental(); saveExperimentalPreference(); });
        experimentalLowLatencyToggle = &experimental.addToggle ("Low-Latency Audio Buffer (128 samples, may glitch or drop routing)", experimentalLowLatency,
            [this] (bool v) { experimentalLowLatency = v; applyAllExperimental(); saveExperimentalPreference(); });
        experimentalAutoFocusToggle = &experimental.addToggle ("Auto-Reclaim Keyboard Focus for Hotkeys (may interrupt clicks/drags)", experimentalAutoReclaimFocus,
            [this] (bool v) { experimentalAutoReclaimFocus = v; applyAllExperimental(); saveExperimentalPreference(); });

        settingsContent.addAndMakeVisible (experimentalStatusLabel);
        experimentalStatusLabel.setFont (juce::Font (11.5f));
        experimentalStatusLabel.setJustificationType (juce::Justification::topLeft);
        experimentalStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        updateExperimentalRowsEnabled();

        auto& performance = addSection ("Performance", "P");
        performance.addCombo ("Audio Buffer Size",
            { "Auto (driver default)", "64 samples", "128 samples", "256 samples", "512 samples", "1024 samples", "2048 samples" },
            performanceBufferSizeIndex,
            [this] (int idx)
            {
                performanceBufferSizeIndex = idx;
                applyPerformanceBufferSize (idx);
                savePerformancePreference();
            });
        performance.addToggle ("Reduce Visual Effects (lowers CPU/GPU usage)", performanceReducedEffects,
            [this] (bool v)
            {
                performanceReducedEffects = v;
                applyPerformanceReducedEffects (v);
                savePerformancePreference();
            });
        performance.addCombo ("UI Refresh Rate", { "Low (10 Hz)", "Normal (20 Hz)", "High (30 Hz)" },
            performanceUiRateIndex,
            [this] (int idx)
            {
                performanceUiRateIndex = idx;
                startTimerHz (kPerformanceUiHz[juce::jlimit (0, 2, idx)]);
                savePerformancePreference();
            });

        settingsContent.addAndMakeVisible (performanceStatusLabel);
        performanceStatusLabel.setFont (juce::Font (11.5f));
        performanceStatusLabel.setJustificationType (juce::Justification::topLeft);
        performanceStatusLabel.setColour (juce::Label::textColourId, palette().textDim);
        performanceStatusLabel.setText ("Lower buffer sizes reduce latency but use more CPU; smaller is not always better.",
            juce::dontSendNotification);


        auto& discord = addSection ("Discord Rich Presence", "D");
        discord.addToggle ("Show \"Using vlab\" on Discord", discordRpcEnabled,
            [this] (bool v) { discordRpcEnabled = v; discordRPC.setEnabled (v); saveDiscordRpcPreference(); });
    }

    // ── build: Visualizer tab (Advanced Mode) ───────────────────────────
    void buildVisualizerTab()
    {
        visualizerContent.setInterceptsMouseClicks (false, true);
        visualizerContent.addAndMakeVisible (visualizerTitleLabel);
        visualizerTitleLabel.setFont (juce::Font (16.0f, juce::Font::bold));
        visualizerTitleLabel.setColour (juce::Label::textColourId, palette().accent);

        visualizerContent.addAndMakeVisible (vizChangerCombo);
        refreshVizChangerCombo();
        vizChangerCombo.onChange = [this] { applyVizChangerSelection(); refreshDiscordPresence(); };

        visualizerContent.addAndMakeVisible (waveformDisplay);
        visualizerContent.addAndMakeVisible (visualizerHeadWidget);
        visualizerHeadWidget.onAzimuthChanged = [this] (float deg)
        {
            engine.params.headSpaceAzimuth = deg;
            settingsHeadWidget.setAzimuth (deg);
        };

        visualizerContent.addAndMakeVisible (statsPanel);
        visualizerContent.addAndMakeVisible (spectrumAnalyzer);
        visualizerContent.addAndMakeVisible (goniometer);
        visualizerContent.addAndMakeVisible (vuMeterDisplay);
        visualizerContent.addAndMakeVisible (radialSpectrum);
        visualizerContent.addAndMakeVisible (modVizDisplay);

        showFeaturedVisualizer();
    }

    void refreshVizChangerCombo()
    {
        vizChangerCombo.clear();
        vizChangerCombo.addItem ("Waveform", 1);
        vizChangerCombo.addItem ("Spectrum", 2);
        vizChangerCombo.addItem ("Goniometer", 3);
        vizChangerCombo.addItem ("Virtual Head", 4);
        vizChangerCombo.addItem ("VU Meters", 5);
        vizChangerCombo.addItem ("Radial Spectrum", 6);
        int id = 100;
        for (auto& m : visualizerModHost.getMods())
            if (m->initOk)
                vizChangerCombo.addItem ("Mod: " + juce::String (m->info.name), id++);
        vizChangerCombo.setSelectedId (1, juce::dontSendNotification);
    }

    void applyVizChangerSelection()
    {
        const int id = vizChangerCombo.getSelectedId();
        if (id == 1) currentVizMode = VizMode::Waveform;
        else if (id == 2) currentVizMode = VizMode::Spectrum;
        else if (id == 3) currentVizMode = VizMode::Goniometer;
        else if (id == 4) currentVizMode = VizMode::Head;
        else if (id == 5) currentVizMode = VizMode::VuMeter;
        else if (id == 6) currentVizMode = VizMode::Radial;
        else if (id >= 100)
        {
            currentVizMode = VizMode::Mod;
            selectedVizModIndex = id - 100;
        }
        showFeaturedVisualizer();
    }

    void showFeaturedVisualizer()
    {
        waveformDisplay.setVisible (currentVizMode == VizMode::Waveform);
        spectrumAnalyzer.setVisible (currentVizMode == VizMode::Spectrum);
        goniometer.setVisible (currentVizMode == VizMode::Goniometer);
        vuMeterDisplay.setVisible (currentVizMode == VizMode::VuMeter);
        radialSpectrum.setVisible (currentVizMode == VizMode::Radial);
        visualizerHeadWidget.setVisible (currentVizMode == VizMode::Head);
        modVizDisplay.setVisible (currentVizMode == VizMode::Mod);
        layoutVisualizerContent (visualizerContent.getWidth() > 0 ? visualizerContent.getWidth() : getWidth(),
                                  visualizerContent.getHeight() > 0 ? visualizerContent.getHeight() : getHeight());
    }

    struct StatsPanel : public juce::Component
    {
        vlab::AudioEngine* engine = nullptr;
        int bufferSize = 0;

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat();
            juce::ColourGradient bg (p.panel.brighter (0.02f), 0, 0, p.panel.darker (0.15f), 0, bounds.getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (bounds, u.cornerRadius);
            g.setColour (p.stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), u.cornerRadius, u.borderWidth);

            auto a = bounds.reduced (14.0f, 10.0f);
            g.setColour (p.accent);
            g.setFont (juce::Font (13.0f, juce::Font::bold));
            g.drawFittedText ("ENGINE STATS", a.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1);

            auto row = [&] (const juce::String& label, const juce::String& value)
            {
                auto r = a.removeFromTop (20);
                g.setColour (p.textDim);
                g.setFont (juce::Font (12.0f));
                g.drawFittedText (label, r.removeFromLeft (140).toNearestInt(), juce::Justification::centredLeft, 1);
                g.setColour (p.text);
                g.setFont (juce::Font (12.0f, juce::Font::bold));
                g.drawFittedText (value, r.toNearestInt(), juce::Justification::centredLeft, 1);
            };

            const float cpu = engine != nullptr ? engine->cpuPercent.load() : 0.0f;
            row ("CPU Load", juce::String ((int) cpu) + "%");
            row ("Sample Rate", juce::String ((int) vlab::kSampleRate) + " Hz");
            row ("Buffer Size", juce::String (bufferSize) + " samples");
            row ("Block Latency", juce::String (bufferSize / (vlab::kSampleRate / 1000.0), 1) + " ms");
        }
    };
    StatsPanel statsPanel;

    // Displays whatever a visualizer mod rendered into a juce::Image.
    struct ModVizDisplay : public juce::Component
    {
        juce::Image image;
        juce::String emptyMessage = "No visualizer mods loaded";

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            const auto& u = uiPreset();
            auto bounds = getLocalBounds().toFloat();
            g.setColour (p.panel.darker (0.1f));
            g.fillRoundedRectangle (bounds, u.cornerRadius);
            if (image.isValid())
            {
                g.drawImage (image, bounds.reduced (2.0f), juce::RectanglePlacement::stretchToFit);
            }
            else
            {
                g.setColour (p.textDim);
                g.setFont (juce::Font (13.0f));
                g.drawText (emptyMessage, getLocalBounds(), juce::Justification::centred);
            }
            g.setColour (p.stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), u.cornerRadius, u.borderWidth);
        }
    };
    ModVizDisplay modVizDisplay;

    void resetSynthPatch()
    {
        heldArpNotes.clear();
        if (sequencer) sequencer->stop();
        synth.allNotesOff();
        holdToneToggle.setToggleState (false, juce::dontSendNotification);
        synthParams.resetToDefaults();

        synthSections.clear();
        pianoKeyButtons.clear();
        synthContent.removeAllChildren();
        buildSynthTab();
        layoutSynthContent (synthViewport.getWidth());
        refreshDiscordPresence();
    }

    // ── build: Synth tab ─────────────────────────────────────────────────
    void buildSynthTab()
    {
        synthContent.setInterceptsMouseClicks (false, true);
        auto& sp = synthParams;

        auto addSection = [this] (const juce::String& title, const juce::String& glyph) -> FxSection&
        {
            auto s = std::make_unique<FxSection> (title, glyph);
            synthContent.addAndMakeVisible (*s);
            synthSections.push_back (std::move (s));
            return *synthSections.back();
        };

        auto& osc1 = addSection ("Oscillator 1", "O");
        osc1.addCombo ("Waveform", { "Sine", "Square", "Saw", "Triangle", "Pulse", "SuperSaw", "Organ", "Wavefolder" }, 0,
            [&sp] (int idx) { sp.wave = (vlab::WaveType) idx; });
        osc1.addSlider ("Unison Voices", 1.0, 7.0, 1.0, [&sp] (double v) { sp.unison = (int) v; }, 0);
        osc1.addSlider ("Unison Detune (cents)", 0.0, 50.0, 10.0, [&sp] (double v) { sp.unisonDetune = (float) v; }, 0);
        osc1.addSlider ("Detune (cents)", -100.0, 100.0, 0.0, [&sp] (double v) { sp.detune = (float) v; }, 0);
        osc1.addSlider ("Portamento (s)", 0.0, 1.0, 0.0, [&sp] (double v) { sp.portamento = (float) v; }, 2);

        auto& osc2 = addSection ("Oscillator 2 / Sub / Noise", "S");
        osc2.addSlider ("OSC2 Mix", 0.0, 1.0, 0.0, [&sp] (double v) { sp.osc2Mix = (float) v; });
        osc2.addSlider ("OSC2 Detune (cents)", -1200.0, 1200.0, 700.0, [&sp] (double v) { sp.osc2Detune = (float) v; }, 0);
        osc2.addCombo ("OSC2 Waveform", { "Sine", "Square", "Saw", "Triangle", "Pulse", "SuperSaw", "Organ", "Wavefolder" }, 2,
            [&sp] (int idx) { sp.osc2Wave = (vlab::WaveType) idx; });
        osc2.addSlider ("Sub Oscillator Mix", 0.0, 1.0, 0.0, [&sp] (double v) { sp.subMix = (float) v; });
        osc2.addSlider ("Noise Mix", 0.0, 1.0, 0.0, [&sp] (double v) { sp.noiseMix = (float) v; });

        auto& fm = addSection ("FM", "F");
        fm.addSlider ("FM Depth", 0.0, 5.0, 0.0, [&sp] (double v) { sp.fmDepth = (float) v; }, 2);
        fm.addSlider ("FM Frequency (Hz)", 1.0, 2000.0, 440.0, [&sp] (double v) { sp.fmFreq = (float) v; }, 0);

        auto& env = addSection ("Envelope (ADSR)", "E");
        env.addSlider ("Attack (s)", 0.001, 3.0, 0.05, [&sp] (double v) { sp.envA = (float) v; }, 3);
        env.addSlider ("Decay (s)", 0.001, 3.0, 0.2, [&sp] (double v) { sp.envD = (float) v; }, 3);
        env.addSlider ("Sustain", 0.0, 1.0, 0.7, [&sp] (double v) { sp.envS = (float) v; });
        env.addSlider ("Release (s)", 0.001, 5.0, 0.3, [&sp] (double v) { sp.envR = (float) v; }, 3);
        env.addSlider ("Velocity Sensitivity", 0.0, 1.0, 0.0, [&sp] (double v) { sp.velocitySensitivity = (float) v; });

        auto& filt = addSection ("Filter & Modulation", "M");
        filt.addCombo ("Filter Type", { "Low-Pass", "High-Pass", "Band-Pass", "Notch" }, 0,
            [&sp] (int idx) { sp.filterType = (vlab::FilterType) idx; });
        filt.addSlider ("Filter Cutoff", 0.01, 1.0, 1.0, [&sp] (double v) { sp.filterCutoff = (float) v; }, 2);
        filt.addSlider ("Filter Resonance", 0.0, 1.0, 0.0, [&sp] (double v) { sp.filterResonance = (float) v; });
        filt.addSlider ("Filter Envelope Amount", -1.0, 1.0, 0.0, [&sp] (double v) { sp.filterEnvAmount = (float) v; });
        filt.addCombo ("Mod LFO Target", { "None", "Pitch", "Amplitude", "Filter", "OSC2 Mix" }, 0,
            [&sp] (int idx) { sp.modTarget = (vlab::ModTarget) idx; });
        filt.addCombo ("Mod LFO Shape", { "Sine", "Triangle", "Square", "Sample & Hold" }, 0,
            [&sp] (int idx) { sp.modLfoShape = (vlab::LfoShape) idx; });
        filt.addSlider ("Mod LFO Rate (Hz)", 0.05, 20.0, 5.0, [&sp] (double v) { sp.modLfoRate = (float) v; }, 2);
        filt.addSlider ("Mod LFO Depth", 0.0, 1.0, 0.0, [&sp] (double v) { sp.modLfoDepth = (float) v; });

        auto& fx = addSection ("Synth FX & Output", "X");
        fx.addSlider ("Drive", 0.0, 1.0, 0.0, [&sp] (double v) { sp.drive = (float) v; });
        fx.addSlider ("Distortion", 0.0, 1.0, 0.0, [&sp] (double v) { sp.fxDist = (float) v; });
        fx.addSlider ("Bitcrush (bits)", 0.0, 16.0, 0.0, [&sp] (double v) { sp.fxBitcrush = (float) v; }, 0);
        fx.addSlider ("Chorus Rate (Hz)", 0.05, 5.0, 0.8, [&sp] (double v) { sp.chorusRate = (float) v; }, 2);
        fx.addSlider ("Chorus Depth", 0.0, 1.0, 0.0, [&sp] (double v) { sp.chorusDepth = (float) v; });
        fx.addSlider ("Chorus Mix", 0.0, 1.0, 0.0, [&sp] (double v) { sp.chorusMix = (float) v; });
        fx.addSlider ("Delay Time (ms)", 10.0, 1500.0, 280.0, [&sp] (double v) { sp.delayTimeMs = (float) v; }, 0);
        fx.addSlider ("Delay Feedback", 0.0, 0.95, 0.3, [&sp] (double v) { sp.delayFeedback = (float) v; });
        fx.addSlider ("Delay Mix", 0.0, 1.0, 0.0, [&sp] (double v) { sp.delayMix = (float) v; });
        fx.addSlider ("Pan", -1.0, 1.0, 0.0, [&sp] (double v) { sp.modPan = (float) v; });
        fx.addSlider ("Master Volume", 0.0, 1.0, 0.5, [&sp] (double v) { sp.masterVol = (float) v; });

        auto& arp = addSection ("Arpeggiator / Song", "A");
        arp.addToggle ("Arp On", false, [this, &sp] (bool v)
        {
            sp.arpOn = v;
            if (! v)
            {
                heldArpNotes.clear();
                synth.songActive = false;
                synth.songSteps.clear();
                synth.noteOff();
            }
            refreshDiscordPresence();
        });
        arp.addSlider ("Arp BPM", 40.0, 300.0, 120.0, [&sp] (double v) { sp.arpBpm = (float) v; }, 0);
        arp.addCombo ("Arp Pattern", { "Up", "Down", "Up / Down", "Random" }, 0, [this, &sp] (int idx)
        {
            sp.arpPattern = (vlab::ArpPattern) juce::jlimit (0, 3, idx);
            rebuildArpSong();
        });
        arp.addCombo ("Arp Note Length", { "1/4 Notes", "1/8 Notes", "1/16 Notes", "1/8 Triplets" }, 1, [this, &sp] (int idx)
        {
            static const float durs[] = { 1.0f, 0.5f, 0.25f, 1.0f / 3.0f };
            sp.arpDivisionBeats = durs[(size_t) juce::jlimit (0, 3, idx)];
            rebuildArpSong();
        });
        arp.addSlider ("Arp Octave Range", 1.0, 3.0, 1.0, [this, &sp] (double v)
        {
            sp.arpOctaveRange = (int) v;
            rebuildArpSong();
        }, 0);

        synthPlaySongCombo = &arp.addCombo ("Song", buildSongNameList(), 0, [this] (int idx)
        {
            const auto names = buildSongNameList();
            if (idx < 0 || idx >= names.size() || ! sequencer) return;
            heldArpNotes.clear(); // a named song and the piano arpeggiator both drive songSteps, don't let them fight
            sequencer->playSong (names[idx].toStdString());
            refreshDiscordPresence();
        });

        synthContent.addAndMakeVisible (reloadSongsButton);
        reloadSongsButton.setTooltip ("Re-scan the save file and mods/songs/*.json for new/edited songs without restarting");
        reloadSongsButton.onClick = [this]
        {
            if (! sequencer) return;
            sequencer->reloadSongs();
            const auto names = buildSongNameList();
            if (synthPlaySongCombo != nullptr)
            {
                const auto keep = synthPlaySongCombo->getText();
                synthPlaySongCombo->clear (juce::dontSendNotification);
                for (auto& n : names) synthPlaySongCombo->addItem (n, synthPlaySongCombo->getNumItems() + 1);
                const int idx = names.indexOf (keep);
                synthPlaySongCombo->setSelectedItemIndex (juce::jmax (0, idx), juce::dontSendNotification);
            }
            const auto keepTop = songBox.getText();
            songBox.clear (juce::dontSendNotification);
            for (auto& n : names) songBox.addItem (n, songBox.getNumItems() + 1);
            const int topIdx = names.indexOf (keepTop);
            songBox.setSelectedItemIndex (juce::jmax (0, topIdx), juce::dontSendNotification);
        };

        synthContent.addAndMakeVisible (synthPlayButton);
        synthContent.addAndMakeVisible (synthStopButton);
        synthContent.addAndMakeVisible (synthResetButton);
        synthPlayButton.getProperties().set ("primary", true);
        synthPlayButton.onClick = [this] { if (synthPlaySongCombo != nullptr) synthPlaySongCombo->onChange(); };
        synthStopButton.getProperties().set ("danger", true);
        synthStopButton.onClick = [this]
        {
            heldArpNotes.clear();
            if (sequencer) sequencer->stop();
            synth.allNotesOff();
            holdToneToggle.setToggleState (false, juce::dontSendNotification);
            refreshDiscordPresence();
        };
        synthResetButton.onClick = [this] { resetSynthPatch(); };

        synthContent.addAndMakeVisible (keyboardLabel);
        keyboardLabel.setText ("Play: click and hold a key (hold several + Arp On for a chord)", juce::dontSendNotification);
        keyboardLabel.setFont (juce::Font (12.0f));
        keyboardLabel.setColour (juce::Label::textColourId, palette().textDim);

        static const std::vector<std::pair<juce::String, bool>> notes = {
            { "C4", false }, { "C#4", true }, { "D4", false }, { "D#4", true }, { "E4", false },
            { "F4", false }, { "F#4", true }, { "G4", false }, { "G#4", true }, { "A4", false },
            { "A#4", true }, { "B4", false }, { "C5", false }
        };
        for (auto& [note, black] : notes)
        {
            auto kb = std::make_unique<PianoKeyButton>();
            kb->isBlack = black;
            kb->onPress = [this, note = note]
            {
                synthParams.freq = (float) vlab::noteNameToFreq (note.toStdString());
                if (synthParams.arpOn.load())
                    addHeldArpNote (note.toStdString());
                else
                    synth.noteOn (vlab::noteToMidi (note.toStdString()), (float) vlab::noteNameToFreq (note.toStdString()));
            };
            kb->onRelease = [this, note = note]
            {
                if (synthParams.arpOn.load())
                    removeHeldArpNote (note.toStdString());
                else
                    synth.noteOff (vlab::noteToMidi (note.toStdString()));
            };
            synthContent.addAndMakeVisible (*kb);
            pianoKeyButtons.push_back (std::move (kb));
        }

        synthContent.addAndMakeVisible (toneLabel);
        toneLabel.setText ("Or hold a single tone at any frequency:", juce::dontSendNotification);
        toneLabel.setFont (juce::Font (12.0f));
        toneLabel.setColour (juce::Label::textColourId, palette().textDim);

        synthContent.addAndMakeVisible (toneFreqSlider);
        toneFreqSlider.setRange (20.0, 5000.0, 1.0);
        toneFreqSlider.setSkewFactorFromMidPoint (440.0);
        toneFreqSlider.setValue (440.0, juce::dontSendNotification);
        toneFreqSlider.setTextValueSuffix (" Hz");
        toneFreqSlider.onValueChange = [this]
        {
            if (holdToneToggle.getToggleState())
                synthParams.freq = (float) toneFreqSlider.getValue();
        };

        synthContent.addAndMakeVisible (holdToneToggle);
        holdToneToggle.onClick = [this]
        {
            if (holdToneToggle.getToggleState())
            {
                synthParams.freq = (float) toneFreqSlider.getValue();
                synth.noteOn();
            }
            else
            {
                synth.noteOff();
            }
        };
    }

    juce::StringArray buildSongNameList()
    {
        juce::StringArray names;
        if (sequencer != nullptr)
        {
            for (auto& [name, song] : sequencer->getSongs())
                names.add (name);
        }
        else // shouldn't happen, sequencer is built eagerly in the constructor, but fall back just in case
        {
            for (auto& [name, song] : vlab::builtinSongs())
                names.add (name);
        }
        return names;
    }

    // ── arpeggiator (held piano keys -> Song) ──────────────────────────────
    void addHeldArpNote (const std::string& note)
    {
        if (std::find (heldArpNotes.begin(), heldArpNotes.end(), note) == heldArpNotes.end())
            heldArpNotes.push_back (note);
        rebuildArpSong();
    }

    void removeHeldArpNote (const std::string& note)
    {
        heldArpNotes.erase (std::remove (heldArpNotes.begin(), heldArpNotes.end(), note), heldArpNotes.end());
        rebuildArpSong();
    }

    void rebuildArpSong()
    {
        if (heldArpNotes.empty())
        {
            synth.songActive = false;
            synth.songSteps.clear();
            synth.noteOff();
            return;
        }

        auto sorted = heldArpNotes;
        std::sort (sorted.begin(), sorted.end(), [] (const std::string& a, const std::string& b)
        {
            return vlab::noteToMidi (a) < vlab::noteToMidi (b);
        });
        sorted.erase (std::unique (sorted.begin(), sorted.end()), sorted.end());

        std::vector<std::string> expanded;
        const int octaves = juce::jlimit (1, 3, synthParams.arpOctaveRange.load());
        for (int o = 0; o < octaves; ++o)
            for (auto& n : sorted)
                expanded.push_back (vlab::transposeOctave (n, o));

        std::vector<std::string> ordered;
        switch (synthParams.arpPattern.load())
        {
            case vlab::ArpPattern::Up:
                ordered = expanded;
                break;
            case vlab::ArpPattern::Down:
                ordered = expanded;
                std::reverse (ordered.begin(), ordered.end());
                break;
            case vlab::ArpPattern::UpDown:
                ordered = expanded;
                for (int i = (int) expanded.size() - 2; i > 0; --i)
                    ordered.push_back (expanded[(size_t) i]);
                break;
            case vlab::ArpPattern::Random:
                ordered = expanded;
                std::shuffle (ordered.begin(), ordered.end(), arpRng);
                break;
        }

        vlab::Song song;
        song.loop = true;
        const float dur = synthParams.arpDivisionBeats.load();
        for (auto& n : ordered)
            song.steps.push_back ({ n, dur, 1.0f, false });

        synth.loadSong (song);
        synth.noteOn(); // retrigger the envelope on every chord change, like a real arp
    }

    // ── build: Mods tab ──────────────────────────────────────────────────
    void buildModsTab()
    {
        modsContent.setInterceptsMouseClicks (false, true);

        modsContent.addAndMakeVisible (modsIntroLabel);
        modsIntroLabel.setText (
            "DLL mods are native shared libraries (.dll/.so/.dylib) that export a small "
            "fixed set of functions (see the SDK header below) and get run on the audio "
            "signal when enabled. IMPORTANT: a native mod is NOT sandboxed - it runs with "
            "the same privileges as the rest of the app, same as any VST plugin. Only "
            "enable mods from sources you trust. What's not built yet: per-mod parameter "
            "UIs, hot-reload without restarting, and drag-to-reorder - mods just run in the "
            "order they were loaded, in sequence, right before the MIDI mix stage.",
            juce::dontSendNotification);
        modsIntroLabel.setFont (juce::Font (12.5f));
        modsIntroLabel.setColour (juce::Label::textColourId, palette().textDim);
        modsIntroLabel.setJustificationType (juce::Justification::topLeft);

        modsContent.addAndMakeVisible (rescanModsButton);
        modsContent.addAndMakeVisible (openModsFolderButton);
        rescanModsButton.getProperties().set ("primary", true);
        rescanModsButton.setButtonText ("Reload All Mods");
        rescanModsButton.setTooltip ("Reloads audio mods, visualizer mods, App Mods, themes, and language mods, everything below, all at once");
        rescanModsButton.onClick = [this] { reloadAllMods(); };
        openModsFolderButton.onClick = [this] { modsFolder.revealToUser(); };

        modsContent.addAndMakeVisible (modsSdkLabel);
        modsSdkLabel.setText (
            "SDK header: Source/Mods/SDK/VLabModAPI.h (in the project source, with a full "
            "example mod in the comment at the top). Build a mod as a shared library and "
            "drop it in the mods/dll folder next to this app, then Reload All Mods above.",
            juce::dontSendNotification);
        modsSdkLabel.setFont (juce::Font (12.0f));
        modsSdkLabel.setColour (juce::Label::textColourId, palette().textDim);
        modsSdkLabel.setJustificationType (juce::Justification::topLeft);

        modsContent.addAndMakeVisible (appModsIntroLabel);
        appModsIntroLabel.setText (
            "App Mods are a different kind of mod entirely, instead of processing audio, "
            "they modify vlab's own interface: adding a real button, slider, or QOL toggle to "
            "their card below, binding a global hotkey, remembering their own settings between "
            "launches, or drawing a fully custom panel (a \"canvas\") that can even take over "
            "the whole main view. See SDK.md section 5 and Source/Mods/SDK/VLabAppModAPI.h. "
            "Drop one in mods/appmods, then use the Reload All Mods button above (or restart) "
            "to load it.",
            juce::dontSendNotification);
        appModsIntroLabel.setFont (juce::Font (12.0f));
        appModsIntroLabel.setColour (juce::Label::textColourId, palette().textDim);
        appModsIntroLabel.setJustificationType (juce::Justification::topLeft);

        rebuildModsList();
        rebuildAppModsUi();
    }

    void rebuildAppModsUi()
    {
        appModsSections.clear();       // FxSection destructors remove themselves from modsContent
        appModEmbeddedCanvases.clear(); // must go before the sections above stop referencing them
        appModFullViewTabs.clear();    // destroys their canvases + top-bar buttons together
        activeFullViewCanvas = nullptr;

        auto& mods = appModHost.getMods();
        if (mods.empty())
        {
            auto& list = *appModsSections.emplace_back (std::make_unique<FxSection> ("App Mods", "A"));
            modsContent.addAndMakeVisible (list);
            list.addToggle ("(none found in mods/appmods, this is normal, not an error)", false, [] (bool) {});
        }
        else
        {
            for (auto& mod : mods)
            {
                juce::String title = juce::String (mod->info.name) + " by " + juce::String (mod->info.author)
                                      + " (v" + juce::String (mod->info.version) + ")";
                auto& section = *appModsSections.emplace_back (std::make_unique<FxSection> (title, "A"));
                modsContent.addAndMakeVisible (section);

                if (juce::String (mod->info.description).isNotEmpty())
                    section.addToggle (juce::String (mod->info.description), false, [] (bool) {}).setEnabled (false);

                auto* modPtr = mod.get();
                for (auto& action : mod->actions)
                {
                    if (action.isToggle)
                    {
                        section.addToggle (action.label, action.toggleState,
                            [this, modPtr, &action] (bool v) { appModHost.toggleAction (*modPtr, action, v); });
                    }
                    else
                    {
                        section.addButton (action.label, action.tooltip,
                            [this, modPtr, &action] { appModHost.triggerButton (*modPtr, action); });
                    }
                }

                for (auto& canvas : mod->canvases)
                {
                    if (canvas.takeOverMainView)
                        continue; // built below, as a top-bar tab, not inline here
                    auto comp = std::make_unique<vlab::ui::AppModCanvasComponent> (appModHost, canvas);
                    section.addCustomComponent (*comp, canvas.preferredH);
                    appModEmbeddedCanvases.push_back (std::move (comp));
                }

                for (auto& canvas : mod->canvases)
                {
                    if (! canvas.takeOverMainView)
                        continue;
                    auto entry = std::make_unique<AppModFullViewTab>();
                    entry->canvas = std::make_unique<vlab::ui::AppModCanvasComponent> (appModHost, canvas);
                    entry->button = std::make_unique<juce::TextButton> (canvas.title);
                    addAndMakeVisible (*entry->button);
                    appModFullViewHost.addChildComponent (*entry->canvas);
                    auto* canvasPtr = entry->canvas.get();
                    entry->button->onClick = [this, canvasPtr] { showAppModCanvas (*canvasPtr); };
                    appModFullViewTabs.push_back (std::move (entry));
                }
            }
        }

        layoutModsContent (modsContent.getWidth() > 0 ? modsContent.getWidth() : getWidth() - getSidebarWidth() - 36);
        resized(); // top bar needs to lay out any new full-view canvas tab buttons
    }

    void showAppModCanvas (vlab::ui::AppModCanvasComponent& canvas)
    {
        boardSelectorBar.setVisible (false);
        if (tabEchoPanel_ != nullptr) tabEchoPanel_->setVisible (false);
        boardViewport.setVisible (false);
        boardSettingsLabel.setVisible (false);
        boardVolumeLabel.setVisible (false);
        boardSpeedLabel.setVisible (false);
        boardVolumeSlider.setVisible (false);
        boardSpeedSlider.setVisible (false);
        boardLoopToggle.setVisible (false);
        boardReverseToggle.setVisible (false);
        fxRackViewport.setVisible (false);
        settingsViewport.setVisible (false);
        visualizerViewport.setVisible (false);
        modsViewport.setVisible (false);
        synthViewport.setVisible (false);
        themeCreatorViewport.setVisible (false);
        ttsViewport.setVisible (false);

        boardTabButton.setToggleState (false, juce::dontSendNotification);
        fxRackTabButton.setToggleState (false, juce::dontSendNotification);
        settingsTabButton.setToggleState (false, juce::dontSendNotification);
        visualizerTabButton.setToggleState (false, juce::dontSendNotification);
        modsTabButton.setToggleState (false, juce::dontSendNotification);
        synthTabButton.setToggleState (false, juce::dontSendNotification);
        themeCreatorTabButton.setToggleState (false, juce::dontSendNotification);
        ttsTabButton.setToggleState (false, juce::dontSendNotification);
        importButton.setVisible (false);

        for (auto& t : appModFullViewTabs)
        {
            t->canvas->setVisible (t->canvas.get() == &canvas);
            t->button->setToggleState (t->canvas.get() == &canvas, juce::dontSendNotification);
        }
        activeFullViewCanvas = &canvas;
        appModFullViewHost.setVisible (true);
        resized();
    }

    // ── build: Theme Creator tab ─────────────────────────────────────────
    void buildThemeCreatorTab()
    {
        themeCreatorContent.setInterceptsMouseClicks (false, true);
        themeCreatorPanel = std::make_unique<ThemeCreatorPanel> (themeManager, themesFolder);
        themeCreatorContent.addAndMakeVisible (*themeCreatorPanel);
        themeCreatorPanel->onThemeSaved = [this]
        {
            themeManager.savePreference (themePrefFile);
            saveStore.commit ("theme");
            refreshDiscordPresence();
            settingsSections.clear();
            buildSettingsTab();
            applyAllExperimental(); // re-sync the freshly-rebuilt Experimental toggles to the current state
            applyAllPerformance();
            layoutSettingsContent (settingsContent.getWidth() > 0 ? settingsContent.getWidth() : getWidth());
            rgbEngine.refreshBase();
        };

        rgbControlPanel = std::make_unique<RgbControlPanel> (rgbEngine);
        themeCreatorContent.addAndMakeVisible (*rgbControlPanel);
        rgbControlPanel->onSettingsChanged = [this] { rgbEngine.savePreference (rgbPrefFile); saveStore.commit ("rgb"); };

        layoutThemeCreatorContent (themeCreatorContent.getWidth() > 0 ? themeCreatorContent.getWidth() : getWidth());
    }

    void applyTranslations()
    {
        auto& tr = translationManager;
        boardTabButton.setButtonText (tr.t ("boardTab"));
        fxRackTabButton.setButtonText (tr.t ("fxRackTab"));
        settingsTabButton.setButtonText (tr.t ("settingsTab"));
        visualizerTabButton.setButtonText (tr.t ("visualizerTab"));
        modsTabButton.setButtonText (tr.t ("modsTab"));
        synthTabButton.setButtonText (tr.t ("synthTab"));
        themeCreatorTabButton.setButtonText (tr.t ("themeCreatorTab"));
        ttsTabButton.setButtonText (tr.t ("ttsTab"));
        importButton.setButtonText (tr.t ("importButton"));
        stopAllSoundsButton.setButtonText (tr.t ("stopAllSounds"));

        stopSongButton.setButtonText (tr.t ("stopSong"));
        reloadSongsButton.setButtonText (tr.t ("reloadSongs"));

        refreshButton.setButtonText (tr.t ("refreshDevices"));
        startButton.setButtonText (tr.t ("startEngine"));

        fxResetButton.setButtonText (tr.t ("fxReset"));
        fxSavePresetButton.setButtonText (tr.t ("fxSavePreset"));
        fxDeletePresetButton.setButtonText (tr.t ("fxDeletePreset"));

        setRecordHotkeyButton.setButtonText (tr.t ("setRecordHotkey"));
        chooseStartupSoundButton.setButtonText (tr.t ("chooseStartupSound"));
        loadBackgroundButton.setButtonText (tr.t ("loadBackground"));
        clearBackgroundButton.setButtonText (tr.t ("clearBackground"));
        openRecordingsFolderButton.setButtonText (tr.t ("openRecordingsFolder"));
        rescanModsButton.setButtonText (tr.t ("reloadAllMods"));
        openModsFolderButton.setButtonText (tr.t ("openModsFolder"));

        synthPlayButton.setButtonText (tr.t ("synthPlay"));
        synthStopButton.setButtonText (tr.t ("synthStop"));

        speakButton.setButtonText (tr.t ("speak"));
        stopTtsButton.setButtonText (tr.t ("stopTts"));
    }

    void reapplyThemeAndMod()
    {
        themeManager.applyTheme (themeManager.getLastAppliedThemeName());
        rgbEngine.refreshBase();
        resized();
        repaint();
        for (auto* c : { (juce::Component*) &boardContent, &fxRackContent, &settingsContent,
                          &visualizerContent, &synthContent, &modsContent, &themeCreatorContent })
            c->repaint();
    }

    void reloadAllMods()
    {
        reloadOverlay = std::make_unique<ReloadOverlay>();
        addAndMakeVisible (*reloadOverlay);
        reloadOverlay->setBounds (getLocalBounds());
        reloadOverlay->toFront (false);

        juce::Component::SafePointer<MainComponent> safeThis (this);

        auto postProgress = [safeThis] (const juce::String& label, int done, int total)
        {
            juce::MessageManager::callAsync ([safeThis, label, done, total]
            {
                if (safeThis == nullptr) return;
                if (safeThis->reloadOverlay != nullptr)
                {
                    safeThis->reloadOverlay->statusText = total > 0
                        ? (label + "... " + juce::String (done) + "/" + juce::String (total))
                        : (label + "...");
                    safeThis->reloadOverlay->repaint();
                }
            });
        };

        const auto modsFolderCopy = modsFolder;
        const auto vizFolderCopy = visualizersFolder;
        const double sampleRate = vlab::kSampleRate;

        std::thread ([safeThis, postProgress, modsFolderCopy, vizFolderCopy, sampleRate]
        {
            constexpr int kDllCooldownMs = 500; // same pacing as loadModsInBackground()

            auto loadedMods = vlab::ModHost::scanDetached (modsFolderCopy, sampleRate, 512,
                [&] (int d, int t) { postProgress ("Loading mods", d, t); }, kDllCooldownMs);

            auto loadedViz = vlab::VisualizerModHost::scanDetached (vizFolderCopy, 320, 260,
                [&] (int d, int t) { postProgress ("Loading visualizer mods", d, t); }, kDllCooldownMs);

            struct ScanResults
            {
                std::vector<std::unique_ptr<vlab::LoadedMod>> mods;
                std::vector<std::unique_ptr<vlab::LoadedVisualizer>> viz;
            };
            auto results = std::make_shared<ScanResults> (ScanResults {
                std::move (loadedMods), std::move (loadedViz) });

            juce::MessageManager::callAsync ([safeThis, postProgress, results]
            {
                if (safeThis != nullptr)
                    safeThis->finishReloadAllMods (std::move (results->mods), std::move (results->viz), postProgress);
            });
        }).detach();
    }

    void finishReloadAllMods (std::vector<std::unique_ptr<vlab::LoadedMod>>&& loadedMods,
                               std::vector<std::unique_ptr<vlab::LoadedVisualizer>>&& loadedViz,
                               const std::function<void (const juce::String&, int, int)>& progress)
    {
        const auto activeThemeName = themeManager.getLastAppliedThemeName();
        const auto activeLanguageName = translationManager.getLastAppliedLanguageName();

        modHost.adopt (std::move (loadedMods));

        visualizerModHost.adopt (std::move (loadedViz));
        if (currentVizMode == VizMode::Mod)
            currentVizMode = VizMode::Waveform; // the mod backing selectedVizModIndex may no longer exist
        selectedVizModIndex = -1;
        refreshVizChangerCombo();
        showFeaturedVisualizer();

        // scan() already unloads first; also refires onActionsChanged -> rebuildAppModsUi()
        appModHost.scan (appModsFolder, [&] (int d, int t) { progress ("Loading app mods", d, t); });

        progress ("Reloading themes", 0, 0);
        themeManager.reloadThemeMods (themesFolder);
        if (! themeManager.applyTheme (activeThemeName))
            themeManager.applyTheme ("Default Green");
        reapplyThemeAndMod();

        progress ("Reloading language mods", 0, 0);
        translationManager.reloadLanguageMods (languagesFolder);
        if (! translationManager.applyLanguage (activeLanguageName))
            translationManager.applyLanguage ("English");
        applyTranslations();

        rebuildModsList();

        settingsSections.clear();
        buildSettingsTab();
        applyAllExperimental();
        applyAllPerformance();
        layoutSettingsContent (settingsContent.getWidth() > 0 ? settingsContent.getWidth() : getWidth());

        refreshDiscordPresence();
        reloadOverlay = nullptr;
    }

    void rebuildModsList()
    {
        modsSections.clear(); // FxSection destructors remove themselves from modsContent

        auto& list = *modsSections.emplace_back (std::make_unique<FxSection> ("Loaded DLL Mods", "D"));
        modsContent.addAndMakeVisible (list);

        if (modHost.getMods().empty())
        {
            list.addToggle ("(none found in mods/dll - this is normal, not an error)", false, [] (bool) {});
        }
        else
        {
            for (auto& mod : modHost.getMods())
            {
                const juce::String status = mod->initOk ? "OK" : "FAILED TO INIT";
                juce::String label = juce::String (mod->info.name) + " by " + juce::String (mod->info.author)
                                      + " (v" + juce::String (mod->info.version) + ") [" + status + "]";
                auto* modPtr = mod.get();
                auto& toggle = list.addToggle (label, mod->enabled, [this, modPtr] (bool v) { modPtr->enabled = v; refreshDiscordPresence(); });
                toggle.setEnabled (mod->initOk);
            }
        }
        layoutModsContent (modsContent.getWidth() > 0 ? modsContent.getWidth() : getWidth() - getSidebarWidth() - 36);
        rebuildModFxSections();
    }

    void rebuildModFxSections()
    {
        modFxSections.clear(); // FxSection destructors remove themselves from fxRackContent

        for (auto& mod : modHost.getMods())
        {
            if (! mod->initOk || mod->params.empty())
                continue;

            auto section = std::make_unique<FxSection> ("Mod: " + juce::String (mod->info.name), "X");
            section->setCollapsible (true);
            section->onCollapsedChanged = [this] { layoutFxRackContent (fxRackContent.getWidth() > 0 ? fxRackContent.getWidth() : getWidth() - getSidebarWidth() - 36); };
            fxRackContent.addAndMakeVisible (*section);

            auto* modPtr = mod.get();
            section->addToggle ("Enabled", modPtr->enabled, [this, modPtr] (bool v) { modPtr->enabled = v; refreshDiscordPresence(); });

            for (int i = 0; i < (int) modPtr->params.size(); ++i)
            {
                const auto& pi = modPtr->params[(size_t) i];
                section->addSlider (juce::String (pi.name), (double) pi.minValue, (double) pi.maxValue,
                    (double) pi.defaultValue,
                    [modPtr, i] (double v)
                    {
                        modPtr->currentValues[(size_t) i] = (float) v;
                        if (modPtr->setParamFn) modPtr->setParamFn (i, (float) v);
                    });
            }
            modFxSections.push_back (std::move (section));
        }

        layoutFxRackContent (fxRackContent.getWidth() > 0 ? fxRackContent.getWidth() : getWidth() - getSidebarWidth() - 36);
    }

    void layoutModsContent (int width)
    {
        int y = 0;
        modsIntroLabel.setBounds (0, y, width, 110);
        y += 122;

        auto btnRow = juce::Rectangle<int> (0, y, width, 30);
        rescanModsButton.setBounds (btnRow.removeFromLeft (width / 2 - 6));
        openModsFolderButton.setBounds (btnRow.removeFromRight (width / 2 - 6));
        y += 44;

        for (auto& s : modsSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }

        modsSdkLabel.setBounds (0, y, width, 60);
        y += 76;
        appModsIntroLabel.setBounds (0, y, width, 68);
        y += 84;

        for (auto& s : appModsSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }

        modsContent.setSize (width, y);
    }

    void layoutThemeCreatorContent (int width)
    {
        int y = 0;
        if (themeCreatorPanel != nullptr)
        {
            const int h = themeCreatorPanel->getPreferredHeight();
            themeCreatorPanel->setBounds (0, y, width, h);
            y += h + 20;
        }
        if (rgbControlPanel != nullptr)
        {
            const int h = rgbControlPanel->getPreferredHeight();
            rgbControlPanel->setBounds (0, y, width, h);
            y += h;
        }
        themeCreatorContent.setSize (width, juce::jmax (y, 200));
    }

    void layoutSynthContent (int width)
    {
        if (modernUi) { layoutSynthModern (width); return; }
        int y = 0;
        for (auto& s : synthSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }

        synthPlayButton.setBounds (16, y, (width - 32) / 2 - 4, 28);
        synthStopButton.setBounds (16 + (width - 32) / 2 + 4, y, (width - 32) / 2 - 4, 28);
        y += 34;
        synthResetButton.setBounds (16, y, juce::jmin (width - 32, 160), 24);
        y += 34;
        reloadSongsButton.setBounds (16, y, juce::jmin (width - 32, 160), 24);
        y += 34;

        keyboardLabel.setBounds (16, y, width - 32, 18);
        y += 22;
        const int keyAreaWidth = width - 32;
        const int whiteKeyW = keyAreaWidth / 7;
        int whiteX = 16;
        for (auto& kb : pianoKeyButtons)
            if (! kb->isBlack) { kb->setBounds (whiteX, y, whiteKeyW - 2, 90); whiteX += whiteKeyW; }
        whiteX = 16;
        int whiteIdx = 0;
        for (auto& kb : pianoKeyButtons)
        {
            if (! kb->isBlack) { ++whiteIdx; continue; }
            kb->setBounds (16 + whiteIdx * whiteKeyW - whiteKeyW / 4, y, whiteKeyW / 2, 55);
            kb->toFront (false);
        }
        y += 110;

        toneLabel.setBounds (16, y, width - 32, 18);
        y += 22;
        auto toneRow = juce::Rectangle<int> (16, y, width - 32, 28);
        holdToneToggle.setBounds (toneRow.removeFromLeft (110));
        toneRow.removeFromLeft (8);
        toneFreqSlider.setBounds (toneRow);
        y += 38;

        synthContent.setSize (width, y);
    }


    void layoutSidebar (juce::Rectangle<int> a)
    {
        if (modernUi) { layoutSidebarModern (a); return; }
        logoLabel.setBounds (a.removeFromTop (40));
        taglineLabel.setBounds (a.removeFromTop (18));
        editionLabel.setBounds (a.removeFromTop (18));
        a.removeFromTop (20);

        auto row = [&] (juce::Label& l, juce::ComboBox& c)
        {
            l.setBounds (a.removeFromTop (16));
            c.setBounds (a.removeFromTop (34));
            a.removeFromTop (12);
        };
        row (micLabel, micBox);
        row (outLabel, outBox);
        row (monLabel, monBox);
        monitorVolumeSlider.setBounds (a.removeFromTop (24));
        a.removeFromTop (12);

        routingStatusLabel.setBounds (a.removeFromTop (30));
        a.removeFromTop (4);
        refreshButton.setBounds (a.removeFromTop (32));
        a.removeFromTop (14);
        startButton.setBounds (a.removeFromTop (40));
        a.removeFromTop (8);
        cpuLabel.setBounds (a.removeFromTop (16));
        a.removeFromTop (8);
        micMonitorToggle.setBounds (a.removeFromTop (26));
        a.removeFromTop (12);
        vuTitleLabel.setBounds (a.removeFromTop (14));
        vuBar.setBounds (a.removeFromTop (18));

        a.removeFromTop (18);
        row (presetLabel, presetBox);
        a.removeFromTop (4);
        row (songLabel, songBox);
        stopSongButton.setBounds (a.removeFromTop (30));
        a.removeFromTop (14);
    }

    void layoutTopBar (juce::Rectangle<int> a)
    {
        if (modernUi) { layoutTopBarModern (a); return; }
        if (visualizerTabButton.isVisible())   { visualizerTabButton.setBounds (a.removeFromRight (82)); a.removeFromRight (6); }
        if (themeCreatorTabButton.isVisible()) { themeCreatorTabButton.setBounds (a.removeFromRight (72)); a.removeFromRight (6); }
        if (modsTabButton.isVisible())         { modsTabButton.setBounds (a.removeFromRight (72)); a.removeFromRight (6); }
        if (settingsTabButton.isVisible())     { settingsTabButton.setBounds (a.removeFromRight (82)); a.removeFromRight (6); }
        if (synthTabButton.isVisible())        { synthTabButton.setBounds (a.removeFromRight (72)); a.removeFromRight (6); }
        if (boardTabButton.isVisible())        { boardTabButton.setBounds (a.removeFromRight (82)); a.removeFromRight (6); }
        if (fxRackTabButton.isVisible())       { fxRackTabButton.setBounds (a.removeFromRight (82)); a.removeFromRight (6); }
        if (ttsTabButton.isVisible())          { ttsTabButton.setBounds (a.removeFromRight (62)); a.removeFromRight (14); }
        stopAllSoundsButton.setBounds (a.removeFromRight (130));
        a.removeFromRight (12);
        if (importButton.isVisible()) { importButton.setBounds (a.removeFromRight (80)); a.removeFromRight (12); }

        if (! appModFullViewTabs.empty())
        {
            for (auto& t : appModFullViewTabs)
            {
                t->button->setBounds (a.removeFromRight (juce::jlimit (60, 140, t->button->getButtonText().length() * 8 + 24)));
                a.removeFromRight (6);
            }
            a.removeFromRight (8);
        }

        searchBox.setBounds (a);
    }

    static int layoutMasonry (const std::vector<FxSection*>& sections, int width, int startY, int minColWidth, int gap = 12)
    {
        const int cols = juce::jmax (1, (width + gap) / (minColWidth + gap));
        const int colW = (width - (cols - 1) * gap) / cols;
        std::vector<int> colY ((size_t) cols, startY);
        for (auto* section : sections)
        {
            size_t best = 0;
            for (size_t c = 1; c < colY.size(); ++c)
                if (colY[c] < colY[best]) best = c;
            const int h = section->getPreferredHeight();
            section->setBounds ((int) best * (colW + gap), colY[best], colW, h);
            colY[best] += h + gap;
        }
        return *std::max_element (colY.begin(), colY.end());
    }

    static void forEachFxSection (juce::Component& parent, const std::function<void (FxSection&)>& fn)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* section = dynamic_cast<FxSection*> (child))
                fn (*section);
            forEachFxSection (*child, fn);
        }
    }

    std::vector<std::pair<juce::TextButton*, Tab>> orderedTabButtons()
    {
        return { { &boardTabButton, Tab::Board }, { &fxRackTabButton, Tab::FxRack }, { &synthTabButton, Tab::Synth },
                 { &ttsTabButton, Tab::Tts }, { &modsTabButton, Tab::Mods }, { &visualizerTabButton, Tab::Visualizer },
                 { &themeCreatorTabButton, Tab::ThemeCreator }, { &settingsTabButton, Tab::Settings } };
    }

    juce::Button* getActiveTabButton()
    {
        for (auto& entry : orderedTabButtons())
            if (entry.first->isVisible() && entry.first->getToggleState())
                return entry.first;
        for (auto& t : appModFullViewTabs)
            if (t->button != nullptr && t->button->getToggleState())
                return t->button.get();
        return nullptr;
    }

    bool switchTabByShortcut (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (! mods.isCommandDown() || mods.isShiftDown() || mods.isAltDown())
            return false;
        const int idx = key.getKeyCode() - (int) '1';
        if (idx < 0 || idx > 8)
            return false;
        int n = 0;
        for (auto& entry : orderedTabButtons())
        {
            if (! entry.first->isVisible())
                continue;
            if (n++ == idx)
            {
                showTab (entry.second);
                return true;
            }
        }
        return false;
    }

    void applyUiMode()
    {
        FxSection::compactMode = modernUi;
        forEachFxSection (*this, [] (FxSection& section) { section.resized(); });
        fxCollapseAllButton.setVisible (modernUi);
        fxExpandAllButton.setVisible (modernUi);
        searchBox.setVisible (! modernUi || currentTab == Tab::Board);
        resized();
        repaint();
    }

    void setAllFxSectionsCollapsed (bool collapse)
    {
        for (auto& section : fxSections)
            if (section->isCollapsible()) section->setCollapsed (collapse);
        for (auto& section : modFxSections)
            if (section->isCollapsible()) section->setCollapsed (collapse);
    }

    void layoutSidebarModern (juce::Rectangle<int> a)
    {
        logoLabel.setBounds (a.removeFromTop (34));
        taglineLabel.setBounds (a.removeFromTop (16));
        editionLabel.setBounds (a.removeFromTop (14));
        a.removeFromTop (10);

        startButton.setBounds (a.removeFromTop (38));
        a.removeFromTop (6);
        routingStatusLabel.setBounds (a.removeFromTop (28));
        cpuLabel.setBounds (a.removeFromTop (16));
        a.removeFromTop (10);

        auto row = [&] (juce::Label& l, juce::ComboBox& c)
        {
            l.setBounds (a.removeFromTop (14));
            c.setBounds (a.removeFromTop (28));
            a.removeFromTop (6);
        };
        row (micLabel, micBox);
        row (outLabel, outBox);
        row (monLabel, monBox);
        monitorVolumeSlider.setBounds (a.removeFromTop (22));
        a.removeFromTop (8);

        refreshButton.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        micMonitorToggle.setBounds (a.removeFromTop (24));
        a.removeFromTop (8);
        vuTitleLabel.setBounds (a.removeFromTop (14));
        vuBar.setBounds (a.removeFromTop (16));

        a.removeFromTop (14);
        row (presetLabel, presetBox);
        row (songLabel, songBox);
        stopSongButton.setBounds (a.removeFromTop (28));
    }

    void layoutTopBarModern (juce::Rectangle<int> a)
    {
        auto stopArea = a.removeFromRight (120);
        stopAllSoundsButton.setBounds (stopArea.removeFromTop (34));
        a.removeFromRight (12);

        std::vector<juce::Button*> tabs;
        for (auto& entry : orderedTabButtons())
            if (entry.first->isVisible())
                tabs.push_back (entry.first);
        for (auto& t : appModFullViewTabs)
            if (t->button != nullptr)
                tabs.push_back (t->button.get());

        juce::Font font (14.0f);
        std::vector<int> widths;
        int total = 0;
        for (auto* b : tabs)
        {
            const int w = juce::jlimit (60, 150, font.getStringWidth (b->getButtonText()) + 28);
            widths.push_back (w);
            total += w + 4;
        }
        const float scale = total > a.getWidth() ? (float) a.getWidth() / (float) juce::jmax (1, total) : 1.0f;

        auto strip = a.removeFromTop (34);
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            tabs[i]->setBounds (strip.removeFromLeft ((int) ((float) widths[i] * scale)));
            strip.removeFromLeft (4);
        }
    }

    void layoutFxRackModern (int width)
    {
        width -= 14;
        int y = 0;
        const bool twoRows = width < 700;

        juce::Rectangle<int> top (0, y, width, 30);
        if (twoRows)
        {
            fxDeletePresetButton.setBounds (top.removeFromRight (70));
            top.removeFromRight (6);
            fxSavePresetButton.setBounds (top.removeFromRight (100));
            top.removeFromRight (6);
            fxPresetCombo.setBounds (top);
            y += 36;

            juce::Rectangle<int> bottom (0, y, width, 30);
            fxResetButton.setBounds (bottom.removeFromRight (170));
            bottom.removeFromRight (6);
            fxExpandAllButton.setBounds (bottom.removeFromRight (90));
            bottom.removeFromRight (6);
            fxCollapseAllButton.setBounds (bottom.removeFromRight (96));
            y += 36;
        }
        else
        {
            fxResetButton.setBounds (top.removeFromRight (170));
            top.removeFromRight (6);
            fxExpandAllButton.setBounds (top.removeFromRight (90));
            top.removeFromRight (6);
            fxCollapseAllButton.setBounds (top.removeFromRight (96));
            top.removeFromRight (14);
            fxDeletePresetButton.setBounds (top.removeFromRight (70));
            top.removeFromRight (6);
            fxSavePresetButton.setBounds (top.removeFromRight (100));
            top.removeFromRight (6);
            fxPresetCombo.setBounds (top);
            y += 38;
        }

        std::vector<FxSection*> sections;
        for (auto& section : fxSections) sections.push_back (section.get());
        for (auto& section : modFxSections) sections.push_back (section.get());
        y = layoutMasonry (sections, width, y, 330);
        fxRackContent.setSize (width, y);
    }

    void placePianoKeys (int x0, int y, int totalWidth, int whiteHeight, int blackHeight)
    {
        const int whiteKeyW = totalWidth / 7;
        int whiteX = x0;
        for (auto& kb : pianoKeyButtons)
            if (! kb->isBlack) { kb->setBounds (whiteX, y, whiteKeyW - 2, whiteHeight); whiteX += whiteKeyW; }
        int whiteIdx = 0;
        for (auto& kb : pianoKeyButtons)
        {
            if (! kb->isBlack) { ++whiteIdx; continue; }
            kb->setBounds (x0 + whiteIdx * whiteKeyW - whiteKeyW / 4, y, whiteKeyW / 2, blackHeight);
            kb->toFront (false);
        }
    }

    void layoutSynthModern (int width)
    {
        width -= 14;
        int y = 0;

        juce::Rectangle<int> transport (0, y, width, 30);
        const int bw = (width - 3 * 8) / 4;
        synthPlayButton.setBounds (transport.removeFromLeft (bw));
        transport.removeFromLeft (8);
        synthStopButton.setBounds (transport.removeFromLeft (bw));
        transport.removeFromLeft (8);
        synthResetButton.setBounds (transport.removeFromLeft (bw));
        transport.removeFromLeft (8);
        reloadSongsButton.setBounds (transport);
        y += 38;

        keyboardLabel.setBounds (0, y, width, 16);
        y += 18;
        placePianoKeys (0, y, width, 78, 48);
        y += 86;

        juce::Rectangle<int> toneRow (0, y, width, 26);
        toneLabel.setBounds (toneRow.removeFromLeft (juce::jlimit (150, 260, width / 3)));
        holdToneToggle.setBounds (toneRow.removeFromLeft (100));
        toneRow.removeFromLeft (8);
        toneFreqSlider.setBounds (toneRow);
        y += 36;

        std::vector<FxSection*> sections;
        for (auto& section : synthSections) sections.push_back (section.get());
        y = layoutMasonry (sections, width, y, 330);
        synthContent.setSize (width, y);
    }

    void layoutTtsModern (int width)
    {
        width -= 14;
        int y = 0;
        ttsLabel.setBounds (0, y, width, 16);
        y += 18;
        juce::Rectangle<int> inputRow (0, y, width, 30);
        stopTtsButton.setBounds (inputRow.removeFromRight (90));
        inputRow.removeFromRight (6);
        speakButton.setBounds (inputRow.removeFromRight (90));
        inputRow.removeFromRight (8);
        ttsInput.setBounds (inputRow);
        y += 42;

        std::vector<FxSection*> sections;
        for (auto& section : ttsSections) sections.push_back (section.get());
        y = layoutMasonry (sections, width, y, 330);
        ttsContent.setSize (width, y);
    }

    void layoutBoardContent (int width)
    {
        const int cols = juce::jmax (2, width / 190);
        const int cardW = (width - (cols - 1) * 14) / cols;
        const int cardH = 130;
        int x = 0, y = 0, col = 0;
        for (auto& c : cards)
        {
            c->setBounds (x, y, cardW, cardH);
            ++col; x += cardW + 14;
            if (col >= cols) { col = 0; x = 0; y += cardH + 14; }
        }
        boardContent.setSize (width, y + (col > 0 ? cardH + 14 : 0));
    }

    void layoutFxRackContent (int width)
    {
        if (modernUi) { layoutFxRackModern (width); return; }
        int y = 0;
        fxResetButton.setBounds (0, y, width, 32);
        y += 40;

        auto presetRow = juce::Rectangle<int> (0, y, width, 28);
        fxDeletePresetButton.setBounds (presetRow.removeFromRight (80));
        presetRow.removeFromRight (8);
        fxSavePresetButton.setBounds (presetRow.removeFromRight (120));
        presetRow.removeFromRight (8);
        fxPresetCombo.setBounds (presetRow);
        y += 40;

        for (auto& s : fxSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }
        for (auto& s : modFxSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }
        fxRackContent.setSize (width, y);
    }

    void layoutTtsContent (int width)
    {
        if (modernUi) { layoutTtsModern (width); return; }
        int y = 0;
        for (auto& s : ttsSections)
        {
            const int h = s->getPreferredHeight();
            s->setBounds (0, y, width, h);
            y += h + 14;
        }
        ttsLabel.setBounds (16, y + 4, width - 32, 16);
        ttsInput.setBounds (16, y + 24, width - 32, 30);
        speakButton.setBounds (16, y + 60, (width - 32) / 2 - 4, 28);
        stopTtsButton.setBounds (16 + (width - 32) / 2 + 4, y + 60, (width - 32) / 2 - 4, 28);
        y += 100;
        ttsContent.setSize (width, y);
    }

    int layoutSettingsBlock (size_t i, int x, int y, int w, bool place)
    {
        auto put = [place] (juce::Component& c, juce::Rectangle<int> r) { if (place) c.setBounds (r); };
        const int startY = y;

        if (i == 4)
        {
            put (experimentalWarningLabel, { x, y, w, 48 });
            y += 58;
        }

        auto& s = settingsSections[i];
        const int h = s->getPreferredHeight();
        put (*s, { x, y, w, h });
        y += h + 14;

        if (i == 0)
        {
            put (startupSoundStatusLabel, { x, y, w, 18 });
            y += 22;
            put (chooseStartupSoundButton, { x, y, w, 28 });
            y += 42;
        }
        else if (i == 1)
        {
            put (backgroundStatusLabel, { x, y, w, 18 });
            y += 24;
            juce::Rectangle<int> btnRow (x, y, w, 30);
            auto left = btnRow.removeFromLeft (w * 2 / 3 - 4);
            auto right = btnRow.removeFromRight (w / 3 - 4);
            put (loadBackgroundButton, left);
            put (clearBackgroundButton, right);
            y += 44;
        }
        else if (i == 2)
        {
            put (settingsHeadWidget, { x, y, juce::jmin (w, 320), 220 });
            y += 220 + 14;
        }
        else if (i == 3)
        {
            put (setRecordHotkeyButton, { x, y, juce::jmin (w, 220), 28 });
            y += 38;
            put (recorderStatusLabel, { x, y, w, 36 });
            y += 46;
            put (sbFolderLabel, { x, y, w, 18 });
            y += 22;
            put (openRecordingsFolderButton, { x, y, w, 28 });
            y += 42;
        }
        else if (i == 4)
        {
            put (experimentalStatusLabel, { x, y, w, 36 });
            y += 46;
        }
        else if (i == 5)
        {
            put (performanceStatusLabel, { x, y, w, 36 });
            y += 46;
        }

        return y - startY;
    }

    void layoutSettingsContent (int width)
    {
        int y = 0;

        if (! modernUi)
        {
            for (size_t i = 0; i < settingsSections.size(); ++i)
                y += layoutSettingsBlock (i, 0, y, width, true);
            settingsContent.setSize (width, y);
            return;
        }

        width -= 14;
        const int gap = 12;
        const int cols = juce::jmax (1, (width + gap) / (360 + gap));
        const int colW = (width - (cols - 1) * gap) / cols;
        std::vector<int> colY ((size_t) cols, 0);
        for (size_t i = 0; i < settingsSections.size(); ++i)
        {
            size_t best = 0;
            for (size_t c = 1; c < colY.size(); ++c)
                if (colY[c] < colY[best]) best = c;
            const int h = layoutSettingsBlock (i, 0, 0, colW, false);
            layoutSettingsBlock (i, (int) best * (colW + gap), colY[best], colW, true);
            colY[best] += h;
        }
        settingsContent.setSize (width, *std::max_element (colY.begin(), colY.end()));
    }

    void layoutVisualizerContent (int width, int height)
    {
        int y = 0;
        auto titleRow = juce::Rectangle<int> (0, y, width, 30);
        visualizerTitleLabel.setBounds (titleRow.removeFromLeft (width - 220));
        vizChangerCombo.setBounds (titleRow.removeFromRight (200));
        y += 40;

        const int featuredH = juce::jmax (260, height - 40 - 160 - 14);
        auto featuredBounds = juce::Rectangle<int> (0, y, width, featuredH);
        waveformDisplay.setBounds (featuredBounds);
        spectrumAnalyzer.setBounds (featuredBounds);
        goniometer.setBounds (featuredBounds);
        vuMeterDisplay.setBounds (featuredBounds);
        radialSpectrum.setBounds (featuredBounds);
        visualizerHeadWidget.setBounds (featuredBounds);
        modVizDisplay.setBounds (featuredBounds);
        y += featuredH + 14;

        statsPanel.setBounds (0, y, width, 150);
        y += 150;

        visualizerContent.setSize (width, juce::jmax (height, y));
    }

    void showTab (Tab tab)
    {
        currentTab = tab;

        activeFullViewCanvas = nullptr;
        appModFullViewHost.setVisible (false);
        for (auto& t : appModFullViewTabs)
        {
            t->canvas->setVisible (false);
            t->button->setToggleState (false, juce::dontSendNotification);
        }
        boardSelectorBar.setVisible (tab == Tab::Board);
        boardViewport.setVisible (tab == Tab::Board);
        boardSettingsLabel.setVisible (tab == Tab::Board);
        boardVolumeLabel.setVisible (tab == Tab::Board);
        boardSpeedLabel.setVisible (tab == Tab::Board);
        boardVolumeSlider.setVisible (tab == Tab::Board);
        boardSpeedSlider.setVisible (tab == Tab::Board);
        boardLoopToggle.setVisible (tab == Tab::Board);
        boardReverseToggle.setVisible (tab == Tab::Board);
        if (tabEchoPanel_ != nullptr)
        {
            tabEchoPanel_->setVisible (tab == Tab::Board);
            if (tab == Tab::Board) tabEchoPanel_->syncFromState();
        }
        fxRackViewport.setVisible (tab == Tab::FxRack);
        settingsViewport.setVisible (tab == Tab::Settings);
        visualizerViewport.setVisible (tab == Tab::Visualizer);
        modsViewport.setVisible (tab == Tab::Mods);
        synthViewport.setVisible (tab == Tab::Synth);
        themeCreatorViewport.setVisible (tab == Tab::ThemeCreator);
        ttsViewport.setVisible (tab == Tab::Tts);

        boardTabButton.setToggleState (tab == Tab::Board, juce::dontSendNotification);
        fxRackTabButton.setToggleState (tab == Tab::FxRack, juce::dontSendNotification);
        settingsTabButton.setToggleState (tab == Tab::Settings, juce::dontSendNotification);
        visualizerTabButton.setToggleState (tab == Tab::Visualizer, juce::dontSendNotification);
        modsTabButton.setToggleState (tab == Tab::Mods, juce::dontSendNotification);
        synthTabButton.setToggleState (tab == Tab::Synth, juce::dontSendNotification);
        themeCreatorTabButton.setToggleState (tab == Tab::ThemeCreator, juce::dontSendNotification);
        ttsTabButton.setToggleState (tab == Tab::Tts, juce::dontSendNotification);

        importButton.setVisible (tab == Tab::Board);
        searchBox.setVisible (! modernUi || tab == Tab::Board);
        resized(); // top bar layout depends on tab-button visibility
        repaint();

        refreshDiscordPresence();

    }

    void saveDiscordRpcPreference()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("enabled", discordRpcEnabled);
        discordRpcPrefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        saveStore.commit ("discordRpc");
    }

    void loadDiscordRpcPreference()
    {
        discordRpcEnabled = true; // on by default
        if (! discordRpcPrefFile.existsAsFile())
            return;
        auto parsed = juce::JSON::parse (discordRpcPrefFile);
        if (! parsed.isObject())
            return;
        discordRpcEnabled = (bool) parsed.getProperty ("enabled", true);
    }

    void refreshDiscordPresence()
    {
        if (currentTab != Tab::Board)
        {
            discordNowPlayingSlot = -1;
            const auto text = tabDiscordState (currentTab);
            if (currentTab == lastGenericDiscordTab && text == lastGenericDiscordText)
                return; // nothing's actually changed, don't spam an identical update
            lastGenericDiscordTab = currentTab;
            lastGenericDiscordText = text;
            discordRPC.setPresence ("Using vlab", text);
            return;
        }

        vlab::SoundboardCore::NowPlayingInfo info;
        if (! soundboard.getNowPlaying (info))
        {
            if (discordNowPlayingSlot != -1)
            {
                discordNowPlayingSlot = -1;
                discordRPC.setPresence ("Using vlab", tabDiscordState (currentTab));
            }
            return;
        }

        if (info.slotIdx == discordNowPlayingSlot)
            return; // already showing this one, Discord's own clock keeps the bar moving

        discordNowPlayingSlot = info.slotIdx;
        const auto boardName = juce::String (soundboard.getBoardName (soundboard.getActiveBoardIndex()));
        const auto details = "Playing \"" + info.name + "\"";

        if (info.loop)
        {
            discordRPC.setPresence (details, "Looping on " + boardName, false);
        }
        else
        {
            const juce::int64 nowEpoch = juce::Time::getCurrentTime().toMilliseconds() / 1000;
            const juce::int64 startEpoch = nowEpoch - (juce::int64) info.elapsedSeconds;
            const juce::int64 endEpoch = startEpoch + (juce::int64) juce::jmax (1.0, info.totalSeconds);
            discordRPC.setPresenceWithRange (details, "On " + boardName, startEpoch, endEpoch);
        }
    }

    static juce::String tabToName (Tab tab)
    {
        switch (tab)
        {
            case Tab::Board:        return "Board";
            case Tab::FxRack:       return "FxRack";
            case Tab::Settings:     return "Settings";
            case Tab::Visualizer:   return "Visualizer";
            case Tab::Mods:         return "Mods";
            case Tab::Synth:        return "Synth";
            case Tab::ThemeCreator: return "ThemeCreator";
            case Tab::Tts:          return "Tts";
        }
        return "Board";
    }

    static bool tabFromName (const juce::String& name, Tab& outTab)
    {
        static const std::pair<const char*, Tab> table[] = {
            { "Board", Tab::Board }, { "FxRack", Tab::FxRack }, { "Settings", Tab::Settings },
            { "Visualizer", Tab::Visualizer }, { "Mods", Tab::Mods }, { "Synth", Tab::Synth },
            { "ThemeCreator", Tab::ThemeCreator }, { "Tts", Tab::Tts },
        };
        for (auto& e : table)
            if (name.equalsIgnoreCase (e.first)) { outTab = e.second; return true; }
        return false;
    }

    juce::String tabDiscordState (Tab tab)
    {
        switch (tab)
        {
            case Tab::Board:        return boardDiscordState();
            case Tab::FxRack:       return fxRackDiscordState();
            case Tab::Settings:     return "In Settings";
            case Tab::Visualizer:   return visualizerDiscordState();
            case Tab::Mods:         return modsDiscordState();
            case Tab::Synth:        return synthDiscordState();
            case Tab::ThemeCreator: return themeDiscordState();
            case Tab::Tts:          return ttsDiscordState();
        }
        return "Using vlab";
    }

    juce::String boardDiscordState()
    {
        return "Browsing the Soundboard";
    }

    juce::String fxRackDiscordState()
    {
        const auto preset = presetBox.getText();
        return preset.isNotEmpty() ? ("FX preset: " + preset) : juce::String ("Tweaking the FX Rack");
    }

    juce::String synthDiscordState()
    {
        if (sequencer != nullptr && ! sequencer->currentSong.empty() && synth.songActive)
            return "Playing \"" + juce::String (sequencer->currentSong) + "\"";
        if (synthParams.arpOn.load())
            return "Arpeggiating";
        return "In the Synth";
    }

    juce::String modsDiscordState()
    {
        juce::StringArray enabledNames;
        for (auto& mod : modHost.getMods())
            if (mod->enabled)
                enabledNames.add (juce::String (mod->info.name));

        if (enabledNames.isEmpty())
            return "Browsing Mods";
        if (enabledNames.size() == 1)
            return "Using mod: " + enabledNames[0];
        return "Using " + juce::String (enabledNames.size()) + " mods (" + enabledNames[0] + " + more)";
    }

    juce::String themeDiscordState()
    {
        const auto name = themeManager.getLastAppliedThemeName();
        return name.isNotEmpty() ? ("Editing theme: " + name) : juce::String ("Creating a Theme");
    }

    juce::String visualizerDiscordState()
    {
        const auto viz = vizChangerCombo.getText();
        return viz.isNotEmpty() ? ("Watching: " + viz) : juce::String ("Watching the Visualizer");
    }

    juce::String ttsDiscordState()
    {
        return soundboard.isTtsSpeaking() ? "Speaking..." : juce::String ("In Live TTS");
    }

    void filterBoard (const juce::String& query)
    {
        for (int i = 0; i < kVisibleSlots; ++i)
        {
            const auto& name = soundboard.getSlots()[(size_t) i].name;
            const bool matches = query.isEmpty() || juce::String (name).containsIgnoreCase (query);
            cards[(size_t) i]->setVisible (matches);
        }
    }

    void loadIntoSlot (int idx)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File(), "*.wav;*.aiff;*.flac;*.ogg;*.mp3");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, idx] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile() && soundboard.loadFile (idx, f.getFullPathName()))
                    cards[(size_t) idx]->refreshFromSlot();
            });
    }

    void showImportMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Import Multiple Sounds...");
        menu.addItem (2, "Import Soundboard (.zip)...");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&importButton),
            [this] (int result)
            {
                if (result == 1) importMultipleFiles();
                else if (result == 2) importZipAsBoard();
            });
    }

    void importMultipleFiles()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import sounds", juce::File(), "*.wav;*.aiff;*.flac;*.ogg;*.mp3");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                const auto files = fc.getResults();
                if (files.isEmpty())
                    return;

                int loaded = 0, nextSlot = 0;
                for (auto& f : files)
                {
                    if (! f.existsAsFile())
                        continue;
                    while (nextSlot < kVisibleSlots && (! soundboard.getSlots()[(size_t) nextSlot].path.empty() || soundboard.getSlots()[(size_t) nextSlot].data != nullptr))
                        ++nextSlot;
                    if (nextSlot >= kVisibleSlots)
                        break;
                    if (soundboard.loadFile (nextSlot, f.getFullPathName(), false)) // save once at the end, below
                    {
                        cards[(size_t) nextSlot]->refreshFromSlot();
                        ++loaded;
                        ++nextSlot;
                    }
                }

                if (loaded > 0 && soundboard.onSaveConfigRequested)
                    soundboard.onSaveConfigRequested();
                filterBoard (searchBox.getText());

                if (loaded < files.size())
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import Sounds",
                        "Loaded " + juce::String (loaded) + " of " + juce::String (files.size())
                            + " files, ran out of empty slots on this board.");
            });
    }

    void importZipAsBoard()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import soundboard", juce::File(), "*.zip");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto zipFile = fc.getResult();
                if (! zipFile.existsAsFile())
                    return;

                juce::ZipFile zip (zipFile);
                if (zip.getNumEntries() == 0)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import Soundboard",
                        "Couldn't read \"" + zipFile.getFileName() + "\" as a zip file, or it was empty.");
                    return;
                }

                auto importsRoot = juce::File::getCurrentWorkingDirectory().getChildFile ("imported_soundboards");
                importsRoot.createDirectory();
                const auto baseName = juce::File::createLegalFileName (zipFile.getFileNameWithoutExtension());
                auto destDir = importsRoot.getChildFile (baseName);
                for (int suffix = 2; destDir.exists(); ++suffix)
                    destDir = importsRoot.getChildFile (baseName + "_" + juce::String (suffix));
                destDir.createDirectory();

                if (zip.uncompressTo (destDir).failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import Soundboard",
                        "Failed to extract \"" + zipFile.getFileName() + "\".");
                    return;
                }

                juce::Array<juce::File> audioFiles;
                destDir.findChildFiles (audioFiles, juce::File::findFiles, true,
                                         "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");
                if (audioFiles.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Import Soundboard",
                        "\"" + zipFile.getFileName() + "\" didn't contain any audio files (.wav/.aiff/.flac/.ogg/.mp3).");
                    return;
                }
                std::sort (audioFiles.begin(), audioFiles.end(),
                           [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

                const int newBoardIdx = soundboard.addBoard (zipFile.getFileNameWithoutExtension().toStdString());
                switchBoard (newBoardIdx); // makes the new board's (empty) slots the live ones

                int loaded = 0;
                for (auto& f : audioFiles)
                {
                    if (loaded >= kVisibleSlots)
                        break;
                    if (soundboard.loadFile (loaded, f.getFullPathName(), false)) // save once at the end, below
                    {
                        cards[(size_t) loaded]->refreshFromSlot();
                        ++loaded;
                    }
                }

                if (soundboard.onSaveConfigRequested) soundboard.onSaveConfigRequested();
                filterBoard (""); // clear any leftover search filter so all the new sounds are visible
                boardSelectorBar.refresh (soundboard);

                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Import Soundboard",
                    "Imported " + juce::String (loaded) + " sound(s) into a new board, \""
                        + zipFile.getFileNameWithoutExtension() + "\".");
            });
    }

    void toggleEngine()
    {
        running = ! running.load();
        const bool isRunning = running.load();
        startButton.setButtonText (isRunning ? "Stop Engine" : "Start Engine");
        startButton.getProperties().set ("primary", ! isRunning);
        startButton.getProperties().set ("danger", isRunning);
        startButton.repaint();
    }
};

class VLabApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "V LAB Lite (C++ Rebuild)"; }
    const juce::String getApplicationVersion() override { return kAppVersionString; }

    static vlab::ui::SplashStyle loadSavedSplashStyle()
    {
        vlab::SaveStore store;
        auto file = store.stagingFileFor ("splashStyle");
        if (! file.existsAsFile())
            return vlab::ui::SplashStyle::Blue;
        auto parsed = juce::JSON::parse (file);
        const int idx = parsed.isObject() ? (int) parsed.getProperty ("style", 0) : 0;
        return (vlab::ui::SplashStyle) juce::jlimit (0, 9, idx);
    }

    void initialise (const juce::String&) override
    {
        splashWindow.reset (new SplashWindow (loadSavedSplashStyle()));
        auto* splashComp = dynamic_cast<vlab::ui::SplashScreen*> (splashWindow->getContentComponent());

        splashComp->onFinished = [this]
        {
            if (mainWindow != nullptr)
                mainWindow->setVisible (true);
            splashWindow = nullptr;
        };

        juce::MessageManager::callAsync ([this, splashComp]
        {
            auto onProgress = [splashComp] (const juce::String& label, int done, int total)
            {
                if (splashComp != nullptr)
                    splashComp->reportLoadingStatus (label, done, total);
            };

            mainWindow.reset (new MainWindow (getApplicationName(), onProgress));
            mainWindow->setVisible (false); // shown in onFinished above, once the splash decides it's done

            if (auto* mc = mainWindow->getMainComponent())
            {
                mc->loadModsInBackground ([splashComp]
                {
                    if (splashComp != nullptr)
                        splashComp->notifyLoadComplete();
                });
            }
            else if (splashComp != nullptr)
            {
                splashComp->notifyLoadComplete();
            }
        });
    }

    void shutdown() override { mainWindow = nullptr; splashWindow = nullptr; }

    class SplashWindow : public juce::DocumentWindow
    {
    public:
        explicit SplashWindow (vlab::ui::SplashStyle style)
            : DocumentWindow ("V LAB", juce::Colour (0xff020814), 0)
        {
            setUsingNativeTitleBar (false);
            setContentOwned (new vlab::ui::SplashScreen (style), false);
            centreWithSize (480, 360);
            setDropShadowEnabled (true);
            setVisible (true);
        }
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, std::function<void (const juce::String&, int, int)> onLoadProgress)
            : DocumentWindow (name, vlab::ui::palette().bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* mc = new MainComponent (std::move (onLoadProgress));
            mainComponent = mc;
            setContentOwned (mc, true);
            centreWithSize (getWidth(), getHeight());
            setResizable (true, false);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        MainComponent* getMainComponent() const { return mainComponent; }

    private:
        MainComponent* mainComponent = nullptr;
    };

private:
    std::unique_ptr<SplashWindow> splashWindow;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (VLabApplication)
