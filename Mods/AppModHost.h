#pragma once
#include "SDK/VLabAppModAPI.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <memory>
#include <functional>
#include <cstring>

namespace vlab
{
    struct AppModAction
    {
        int id = 0;
        juce::String label, tooltip;
        bool isToggle = false;
        bool toggleState = false;
    };

    struct AppModHotkeyBinding
    {
        int id = 0;
        juce::String keyDescription; // lowercase, e.g. "ctrl + shift + m"
        juce::String label;
    };

    struct AppModCanvasInfo
    {
        int id = 0;
        juce::String title;
        int preferredW = 320, preferredH = 220;
        bool takeOverMainView = false;
        VLabAppModCanvasPaintFn  paintFn  = nullptr;
        VLabAppModCanvasResizeFn resizeFn = nullptr;
        VLabAppModCanvasMouseFn  mouseFn  = nullptr;
        VLabAppModCanvasKeyFn    keyFn    = nullptr;
    };

    struct AppModSlider
    {
        int id = 0;
        juce::String label, tooltip;
        float minValue = 0.0f, maxValue = 1.0f, stepSize = 0.0f, value = 0.0f;
    };

    struct LoadedAppMod
    {
        juce::String fileName;
        VLabAppModInfo info {};
        std::unique_ptr<juce::DynamicLibrary> lib;
        VLabAppModTickFn tickFn = nullptr;
        VLabAppModOnActionFn onActionFn = nullptr;
        VLabAppModOnSliderFn onSliderFn = nullptr;
        VLabAppModShutdownFn shutdownFn = nullptr;
        bool initOk = false;

        std::vector<AppModAction> actions;
        std::vector<AppModHotkeyBinding> hotkeys;
        std::vector<AppModCanvasInfo> canvases;
        std::vector<AppModSlider> sliders;

        juce::var dataStore;
        bool dataStoreLoaded = false;
        juce::File dataStoreFile;
    };

    class AppModHost
    {
    public:
        std::function<juce::String()> getAppVersion;
        std::function<juce::String()> getActiveTabName;
        std::function<void()> checkForUpdates;
        std::function<void (juce::String)> switchTab;
        std::function<void (juce::String)> showMessage;
        std::function<void()> quitApp;
        std::function<juce::String (juce::String, juce::String)> runCommand; // returns "" for "unknown command"
        std::function<bool()> isAdvancedModeFn;
        std::function<void()> reloadAllModsFn;

        std::function<void()> onActionsChanged;

        ~AppModHost() { unloadAll(); }

        void scan (const juce::File& folder, const std::function<void (int, int)>& onProgress = {})
        {
            unloadAll();
            folder.createDirectory();
            modsFolder = folder;
            folder.getChildFile (".moddata").createDirectory();

           #if JUCE_WINDOWS
            const juce::String ext = "*.dll";
           #elif JUCE_MAC
            const juce::String ext = "*.dylib";
           #else
            const juce::String ext = "*.so";
           #endif

            auto files = folder.findChildFiles (juce::File::findFiles, false, ext);
            const int total = files.size();
            int done = 0;
            for (auto& f : files)
            {
                tryLoad (f);
                ++done;
                if (onProgress) onProgress (done, total);
            }

            if (onActionsChanged) onActionsChanged();
        }

        // Call once per frame from vlab's own UI timer.
        void tick()
        {
            for (auto& m : mods)
            {
                if (! m->tickFn) continue;
                loadingModPtr() = m.get();
                m->tickFn();
                loadingModPtr() = nullptr;
            }
        }

        int getNumLoaded() const { return (int) mods.size(); }

        juce::String getLoadedNamesForDisplay() const
        {
            juce::StringArray names;
            for (auto& m : mods) names.add (juce::String (m->info.name));
            return names.joinIntoString (", ");
        }

        std::vector<std::unique_ptr<LoadedAppMod>>& getMods() { return mods; }

        void triggerButton (LoadedAppMod& m, AppModAction& a)
        {
            if (! m.onActionFn) return;
            loadingModPtr() = &m;
            m.onActionFn (a.id, false);
            loadingModPtr() = nullptr;
        }

        void toggleAction (LoadedAppMod& m, AppModAction& a, bool newState)
        {
            a.toggleState = newState;
            if (! m.onActionFn) { loadingModPtr() = nullptr; return; }
            loadingModPtr() = &m;
            m.onActionFn (a.id, newState);
            loadingModPtr() = nullptr;
        }

        void setSliderFromUi (LoadedAppMod& m, AppModSlider& s, float newValue)
        {
            s.value = juce::jlimit (s.minValue, s.maxValue, newValue);
            if (! m.onSliderFn) return;
            loadingModPtr() = &m;
            m.onSliderFn (s.id, s.value);
            loadingModPtr() = nullptr;
        }

        bool handleKeyPress (const juce::String& lowerCaseDesc)
        {
            for (auto& m : mods)
                for (auto& hk : m->hotkeys)
                    if (hk.keyDescription == lowerCaseDesc)
                    {
                        if (m->onActionFn)
                        {
                            loadingModPtr() = m.get();
                            m->onActionFn (hk.id, false);
                            loadingModPtr() = nullptr;
                        }
                        return true;
                    }
            return false;
        }

        bool paintCanvas (AppModCanvasInfo& c, int w, int h, juce::Image& out)
        {
            if (c.paintFn == nullptr || w <= 0 || h <= 0)
                return false;

            if (out.getWidth() != w || out.getHeight() != h || ! out.isValid())
            {
                out = juce::Image (juce::Image::ARGB, w, h, true);
                if (c.resizeFn) c.resizeFn (c.id, w, h);
            }

            const size_t needed = (size_t) w * (size_t) h * 4;
            if (scratch.size() < needed) scratch.resize (needed);
            c.paintFn (c.id, scratch.data(), w, h, w * 4);

            juce::Image::BitmapData bmp (out, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                auto* dst = bmp.getLinePointer (y);
                const unsigned char* src = scratch.data() + (size_t) y * (size_t) w * 4;
                for (int x = 0; x < w; ++x)
                {
                    const unsigned char r = src[x * 4 + 0], g = src[x * 4 + 1],
                                        b = src[x * 4 + 2], a = src[x * 4 + 3];
                    auto* px = dst + x * 4;
                    px[0] = b; px[1] = g; px[2] = r; px[3] = a;
                }
            }
            return true;
        }

        void sendMouse (AppModCanvasInfo& c, int eventType, int x, int y, int buttons)
        {
            if (c.mouseFn) c.mouseFn (c.id, eventType, x, y, buttons);
        }

        void sendKey (AppModCanvasInfo& c, int keyCode, bool isDown, const juce::String& textDescription)
        {
            if (c.keyFn) c.keyFn (c.id, keyCode, isDown, textDescription.toRawUTF8());
        }

    private:
        std::vector<std::unique_ptr<LoadedAppMod>> mods;
        std::vector<unsigned char> scratch;
        VLabAppHostAPI hostApi {};
        juce::File modsFolder;

        static AppModHost*& instancePtr() { static AppModHost* p = nullptr; return p; }
        static LoadedAppMod*& loadingModPtr() { static LoadedAppMod* p = nullptr; return p; }

        static void copyToOutBuf (const juce::String& s, char* out, int len)
        {
            if (out == nullptr || len <= 0) return;
            const auto utf8 = s.toRawUTF8();
            const int n = juce::jmin (len - 1, (int) strlen (utf8));
            std::memcpy (out, utf8, (size_t) n);
            out[n] = '\0';
        }

        static void VLAB_APPMOD_CALL trampolineGetAppVersion (char* out, int len)
        {
            auto* self = instancePtr();
            copyToOutBuf (self && self->getAppVersion ? self->getAppVersion() : juce::String(), out, len);
        }

        static void VLAB_APPMOD_CALL trampolineGetActiveTab (char* out, int len)
        {
            auto* self = instancePtr();
            copyToOutBuf (self && self->getActiveTabName ? self->getActiveTabName() : juce::String(), out, len);
        }

        static void VLAB_APPMOD_CALL trampolineCheckForUpdates()
        {
            auto* self = instancePtr();
            if (self && self->checkForUpdates) self->checkForUpdates();
        }

        static void VLAB_APPMOD_CALL trampolineSwitchTab (const char* tabName)
        {
            auto* self = instancePtr();
            if (self && self->switchTab) self->switchTab (juce::String (tabName != nullptr ? tabName : ""));
        }

        static void VLAB_APPMOD_CALL trampolineShowMessage (const char* msg)
        {
            auto* self = instancePtr();
            if (self && self->showMessage) self->showMessage (juce::String (msg != nullptr ? msg : ""));
        }

        static void VLAB_APPMOD_CALL trampolineQuitApp()
        {
            auto* self = instancePtr();
            if (self && self->quitApp) self->quitApp();
        }

        static bool VLAB_APPMOD_CALL trampolineRunCommand (const char* name, const char* argJson,
                                                            char* outResult, int outResultLen)
        {
            auto* self = instancePtr();
            if (self == nullptr || ! self->runCommand)
                return false;
            const auto result = self->runCommand (juce::String (name != nullptr ? name : ""),
                                                   juce::String (argJson != nullptr ? argJson : ""));
            if (result.isEmpty())
                return false; // "" is this host's convention for "unknown command"
            copyToOutBuf (result, outResult, outResultLen);
            return true;
        }

        static bool VLAB_APPMOD_CALL trampolineRegisterButton (int id, const char* label, const char* tooltip)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr || mod->actions.size() >= (size_t) VLAB_APPMOD_MAX_ACTIONS)
                return false;
            AppModAction a;
            a.id = id;
            a.label = juce::String (label != nullptr ? label : "");
            a.tooltip = juce::String (tooltip != nullptr ? tooltip : "");
            mod->actions.push_back (a);
            return true;
        }

        static bool VLAB_APPMOD_CALL trampolineRegisterToggle (int id, const char* label, const char* tooltip, bool initialState)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr || mod->actions.size() >= (size_t) VLAB_APPMOD_MAX_ACTIONS)
                return false;
            AppModAction a;
            a.id = id;
            a.label = juce::String (label != nullptr ? label : "");
            a.tooltip = juce::String (tooltip != nullptr ? tooltip : "");
            a.isToggle = true;
            a.toggleState = initialState;
            mod->actions.push_back (a);
            return true;
        }

        static bool VLAB_APPMOD_CALL trampolineRegisterHotkey (int id, const char* keyDescription, const char* label)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr) return false;
            const auto desc = juce::String (keyDescription != nullptr ? keyDescription : "").trim().toLowerCase();
            if (desc.isEmpty()) return false;

            auto* self = instancePtr();
            if (self != nullptr)
                for (auto& other : self->mods)
                    for (auto& hk : other->hotkeys)
                        if (hk.keyDescription == desc)
                            return false; // already claimed by another App Mod

            AppModHotkeyBinding hk;
            hk.id = id;
            hk.keyDescription = desc;
            hk.label = juce::String (label != nullptr ? label : "");
            mod->hotkeys.push_back (hk);
            return true;
        }

        static void VLAB_APPMOD_CALL trampolineSetToggleState (int id, bool state)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr) return;
            bool changed = false;
            for (auto& a : mod->actions)
                if (a.isToggle && a.id == id) { a.toggleState = state; changed = true; break; }
            auto* self = instancePtr();
            if (changed && self != nullptr && self->onActionsChanged) self->onActionsChanged();
        }

        static bool VLAB_APPMOD_CALL trampolineRegisterSlider (int id, const char* label, const char* tooltip,
                                                                float minV, float maxV, float step, float initial)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr || mod->sliders.size() >= (size_t) VLAB_APPMOD_MAX_SLIDERS)
                return false;
            AppModSlider s;
            s.id = id;
            s.label = juce::String (label != nullptr ? label : "");
            s.tooltip = juce::String (tooltip != nullptr ? tooltip : "");
            s.minValue = minV;
            s.maxValue = juce::jmax (minV, maxV);
            s.stepSize = juce::jmax (0.0f, step);
            s.value = juce::jlimit (s.minValue, s.maxValue, initial);
            mod->sliders.push_back (s);
            return true;
        }

        static void VLAB_APPMOD_CALL trampolineSetSliderValue (int id, float value)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr) return;
            bool changed = false;
            for (auto& s : mod->sliders)
                if (s.id == id) { s.value = juce::jlimit (s.minValue, s.maxValue, value); changed = true; break; }
            auto* self = instancePtr();
            if (changed && self != nullptr && self->onActionsChanged) self->onActionsChanged();
        }

        static void ensureDataStoreLoaded (LoadedAppMod& mod, AppModHost& host)
        {
            if (mod.dataStoreLoaded) return;
            mod.dataStoreFile = host.modsFolder.getChildFile (".moddata").getChildFile (mod.fileName + ".json");
            if (mod.dataStoreFile.existsAsFile())
                mod.dataStore = juce::JSON::parse (mod.dataStoreFile);
            if (! mod.dataStore.isObject())
                mod.dataStore = juce::var (new juce::DynamicObject());
            mod.dataStoreLoaded = true;
        }

        static bool VLAB_APPMOD_CALL trampolineGetModDataString (const char* key, char* outBuf, int outBufLen)
        {
            auto* mod = loadingModPtr();
            auto* self = instancePtr();
            if (mod == nullptr || self == nullptr || key == nullptr) return false;
            ensureDataStoreLoaded (*mod, *self);
            if (auto* obj = mod->dataStore.getDynamicObject())
            {
                const auto k = juce::Identifier (key);
                if (! obj->hasProperty (k)) return false;
                copyToOutBuf (obj->getProperty (k).toString(), outBuf, outBufLen);
                return true;
            }
            return false;
        }

        static void VLAB_APPMOD_CALL trampolineSetModDataString (const char* key, const char* value)
        {
            auto* mod = loadingModPtr();
            auto* self = instancePtr();
            if (mod == nullptr || self == nullptr || key == nullptr) return;
            ensureDataStoreLoaded (*mod, *self);
            if (auto* obj = mod->dataStore.getDynamicObject())
            {
                obj->setProperty (juce::Identifier (key), juce::String (value != nullptr ? value : ""));
                mod->dataStoreFile.replaceWithText (juce::JSON::toString (mod->dataStore, true));
            }
        }

        static bool VLAB_APPMOD_CALL trampolineIsAdvancedMode()
        {
            auto* self = instancePtr();
            return self != nullptr && self->isAdvancedModeFn && self->isAdvancedModeFn();
        }

        static void VLAB_APPMOD_CALL trampolineReloadAllMods()
        {
            auto* self = instancePtr();
            if (self != nullptr && self->reloadAllModsFn) self->reloadAllModsFn();
        }

        static bool VLAB_APPMOD_CALL trampolineRegisterCanvas (int id, const char* title, int prefW, int prefH, bool takeOver)
        {
            auto* mod = loadingModPtr();
            if (mod == nullptr || mod->canvases.size() >= (size_t) VLAB_APPMOD_MAX_CANVASES)
                return false;
            if (takeOver)
                for (auto& c : mod->canvases)
                    if (c.takeOverMainView)
                        return false; // only one full-view canvas per mod

            AppModCanvasInfo c;
            c.id = id;
            c.title = juce::String (title != nullptr && title[0] != '\0' ? title : ("Canvas " + juce::String (id)));
            c.preferredW = juce::jmax (32, prefW);
            c.preferredH = juce::jmax (32, prefH);
            c.takeOverMainView = takeOver;
            c.paintFn  = (VLabAppModCanvasPaintFn)  mod->lib->getFunction ("vlab_appmod_canvas_paint");
            c.resizeFn = (VLabAppModCanvasResizeFn) mod->lib->getFunction ("vlab_appmod_canvas_resized");
            c.mouseFn  = (VLabAppModCanvasMouseFn)  mod->lib->getFunction ("vlab_appmod_canvas_mouse");
            c.keyFn    = (VLabAppModCanvasKeyFn)    mod->lib->getFunction ("vlab_appmod_canvas_key");
            mod->canvases.push_back (c);
            return true;
        }

        void tryLoad (const juce::File& f)
        {
            auto lib = std::make_unique<juce::DynamicLibrary>();
            if (! lib->open (f.getFullPathName()))
                return;

            auto getInfoFn  = (VLabAppModGetInfoFn)  lib->getFunction ("vlab_appmod_get_info");
            auto initFn     = (VLabAppModInitFn)     lib->getFunction ("vlab_appmod_init");
            auto tickFn     = (VLabAppModTickFn)     lib->getFunction ("vlab_appmod_tick");
            auto onActionFn = (VLabAppModOnActionFn) lib->getFunction ("vlab_appmod_on_action");
            auto onSliderFn = (VLabAppModOnSliderFn) lib->getFunction ("vlab_appmod_on_slider");
            auto shutdownFn = (VLabAppModShutdownFn) lib->getFunction ("vlab_appmod_shutdown");

            if (getInfoFn == nullptr || initFn == nullptr || shutdownFn == nullptr)
                return; // not a valid App Mod, silently skip, same tolerance ModHost has for stray DLLs

            VLabAppModInfo info {};
            getInfoFn (&info);
            if (info.sdkVersion != VLAB_APPMOD_SDK_VERSION)
                return;

            auto mod = std::make_unique<LoadedAppMod>();
            mod->fileName = f.getFileName();
            mod->info = info;
            mod->lib = std::move (lib);
            mod->tickFn = tickFn;
            mod->onActionFn = onActionFn;
            mod->onSliderFn = onSliderFn;
            mod->shutdownFn = shutdownFn;

            instancePtr() = this; // trampolines above read this
            hostApi.getAppVersion    = trampolineGetAppVersion;
            hostApi.getActiveTabName = trampolineGetActiveTab;
            hostApi.checkForUpdates  = trampolineCheckForUpdates;
            hostApi.switchTab        = trampolineSwitchTab;
            hostApi.showMessage      = trampolineShowMessage;
            hostApi.quitApp          = trampolineQuitApp;
            hostApi.runCommand       = trampolineRunCommand;
            hostApi.registerButton   = trampolineRegisterButton;
            hostApi.registerToggle   = trampolineRegisterToggle;
            hostApi.registerHotkey   = trampolineRegisterHotkey;
            hostApi.setToggleState   = trampolineSetToggleState;
            hostApi.registerCanvas   = trampolineRegisterCanvas;
            hostApi.registerSlider   = trampolineRegisterSlider;
            hostApi.setSliderValue   = trampolineSetSliderValue;
            hostApi.getModDataString = trampolineGetModDataString;
            hostApi.setModDataString = trampolineSetModDataString;
            hostApi.isAdvancedMode   = trampolineIsAdvancedMode;
            hostApi.reloadAllMods    = trampolineReloadAllMods;
            hostApi.hostUserData     = nullptr;

            loadingModPtr() = mod.get(); // registerButton/Toggle/Hotkey/Canvas only work while this is set
            mod->initOk = initFn (&hostApi);
            loadingModPtr() = nullptr;

            if (! mod->initOk)
                return; // mod declined to load, mod->lib destructor unloads it

            mods.push_back (std::move (mod));
        }

        void unloadAll()
        {
            for (auto& m : mods)
                if (m->shutdownFn) m->shutdownFn();
            mods.clear();
        }
    };
}
