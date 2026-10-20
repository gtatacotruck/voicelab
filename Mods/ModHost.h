#pragma once
#include "SDK/VLabModAPI.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

namespace vlab
{
    struct LoadedMod
    {
        juce::String fileName;
        VLabModInfo info {};
        std::unique_ptr<juce::DynamicLibrary> lib;
        VLabModProcessFn processFn = nullptr;
        VLabModSetParamFn setParamFn = nullptr;
        VLabModGetParamFn getParamFn = nullptr;
        VLabModShutdownFn shutdownFn = nullptr;
        bool enabled = false;
        bool initOk = false;

        // Optional, only populated if the mod exports both metadata functions.
        std::vector<VLabModParamInfo> params;
        std::vector<float> currentValues; // mirrors what the host last sent via setParamFn, for slider init
    };

    class ModHost
    {
    public:
        ~ModHost() { unloadAll(); }

        void scanAndLoad (const juce::File& folder, double sampleRate, int maxBlockSize,
                           const std::function<void (int, int)>& onProgress = {})
        {
            if (! folder.isDirectory())
                return;

            auto files = findModFiles (folder);
            const int total = files.size();
            int done = 0;
            for (auto& f : files)
            {
                bool already = std::any_of (mods.begin(), mods.end(),
                                             [&] (auto& m) { return m->fileName == f.getFileName(); });
                if (! already)
                {
                    juce::String err;
                    if (auto mod = loadOne (f, sampleRate, maxBlockSize, err))
                    {
                        const juce::ScopedLock sl (modsLock);
                        mods.push_back (std::move (mod));
                    }
                    else if (err.isNotEmpty())
                    {
                        lastError = err;
                    }
                }
                ++done;
                if (onProgress) onProgress (done, total);
            }
        }

        static std::vector<std::unique_ptr<LoadedMod>> scanDetached (
            const juce::File& folder, double sampleRate, int maxBlockSize,
            const std::function<void (int, int)>& onProgress = {},
            int cooldownMs = 0)
        {
            std::vector<std::unique_ptr<LoadedMod>> result;
            if (! folder.isDirectory())
                return result;

            auto files = findModFiles (folder);
            const int total = files.size();
            int done = 0;
            for (auto& f : files)
            {
                juce::String err;
                if (auto mod = loadOne (f, sampleRate, maxBlockSize, err))
                    result.push_back (std::move (mod));
                ++done;
                if (onProgress) onProgress (done, total);

                if (cooldownMs > 0 && done < total)
                    juce::Thread::sleep (cooldownMs);
            }
            return result;
        }

        void adopt (std::vector<std::unique_ptr<LoadedMod>>&& loaded)
        {
            std::vector<std::unique_ptr<LoadedMod>> old;
            {
                const juce::ScopedLock sl (modsLock);
                old = std::move (mods);
                mods = std::move (loaded);
            }
            for (auto& m : old)
                if (m->shutdownFn) m->shutdownFn();
        }

        void tryLoad (const juce::File& file, double sampleRate, int maxBlockSize)
        {
            juce::String err;
            if (auto mod = loadOne (file, sampleRate, maxBlockSize, err))
            {
                const juce::ScopedLock sl (modsLock);
                mods.push_back (std::move (mod));
            }
            else if (err.isNotEmpty())
            {
                lastError = err;
            }
        }

        void unloadAll()
        {
            std::vector<std::unique_ptr<LoadedMod>> old;
            {
                const juce::ScopedLock sl (modsLock);
                old = std::move (mods);
                mods.clear();
            }
            for (auto& m : old)
                if (m->shutdownFn) m->shutdownFn();
        }

        void process (vlab::AudioBuffer& buf, int numSamples)
        {
            const juce::ScopedLock sl (modsLock);

            if (mods.empty())
                return;
            bool anyEnabled = std::any_of (mods.begin(), mods.end(), [] (auto& m) { return m->enabled && m->initOk; });
            if (! anyEnabled)
                return;

            interleaved.resize ((size_t) numSamples * 2);
            for (int i = 0; i < numSamples; ++i)
            {
                interleaved[(size_t) i * 2]     = buf.getSample (0, i);
                interleaved[(size_t) i * 2 + 1] = buf.getSample (1, i);
            }

            for (auto& m : mods)
                if (m->enabled && m->initOk && m->processFn)
                    m->processFn (interleaved.data(), numSamples, 2);

            for (int i = 0; i < numSamples; ++i)
            {
                buf.setSample (0, i, interleaved[(size_t) i * 2]);
                buf.setSample (1, i, interleaved[(size_t) i * 2 + 1]);
            }
        }

        // Message-thread-only.
        std::vector<std::unique_ptr<LoadedMod>>& getMods() { return mods; }
        juce::String lastError;

    private:
        static juce::Array<juce::File> findModFiles (const juce::File& folder)
        {
#if JUCE_WINDOWS
            const char* pattern = "*.dll";
#elif JUCE_MAC
            const char* pattern = "*.dylib";
#else
            const char* pattern = "*.so";
#endif
            return folder.findChildFiles (juce::File::findFiles, false, pattern);
        }

        static std::unique_ptr<LoadedMod> loadOne (const juce::File& file, double sampleRate, int maxBlockSize,
                                                     juce::String& outError)
        {
            auto mod = std::make_unique<LoadedMod>();
            mod->fileName = file.getFileName();
            mod->lib = std::make_unique<juce::DynamicLibrary>();
            if (! mod->lib->open (file.getFullPathName()))
            {
                outError = "Could not open " + file.getFileName();
                return nullptr;
            }

            auto getInfoFn = (VLabModGetInfoFn) mod->lib->getFunction ("vlab_mod_get_info");
            auto initFn    = (VLabModInitFn)    mod->lib->getFunction ("vlab_mod_init");
            mod->processFn   = (VLabModProcessFn)  mod->lib->getFunction ("vlab_mod_process");
            mod->setParamFn  = (VLabModSetParamFn) mod->lib->getFunction ("vlab_mod_set_param");
            mod->getParamFn  = (VLabModGetParamFn) mod->lib->getFunction ("vlab_mod_get_param");
            mod->shutdownFn  = (VLabModShutdownFn) mod->lib->getFunction ("vlab_mod_shutdown");

            if (getInfoFn == nullptr || initFn == nullptr || mod->processFn == nullptr || mod->shutdownFn == nullptr)
            {
                outError = file.getFileName() + " is missing required exports (see VLabModAPI.h)";
                return nullptr; // mod->lib destructor closes the library
            }

            getInfoFn (&mod->info);
            if (mod->info.sdkVersion != VLAB_MOD_SDK_VERSION)
            {
                outError = file.getFileName() + " was built for a different SDK version";
                return nullptr;
            }

            mod->initOk = initFn (sampleRate, maxBlockSize);

            if (mod->initOk)
            {
                auto getCountFn = (VLabModGetParamCountFn) mod->lib->getFunction ("vlab_mod_get_param_count");
                auto getInfoForParamFn = (VLabModGetParamInfoFn) mod->lib->getFunction ("vlab_mod_get_param_info");
                if (getCountFn != nullptr && getInfoForParamFn != nullptr)
                {
                    const int count = juce::jlimit (0, 64, getCountFn()); // sanity cap
                    for (int i = 0; i < count; ++i)
                    {
                        VLabModParamInfo pi {};
                        getInfoForParamFn (i, &pi);
                        mod->params.push_back (pi);
                        mod->currentValues.push_back (pi.defaultValue);
                        if (mod->setParamFn) mod->setParamFn (i, pi.defaultValue); // seed the mod with its own default
                    }
                }
            }

            return mod;
        }

        std::vector<std::unique_ptr<LoadedMod>> mods;
        std::vector<float> interleaved;
        juce::CriticalSection modsLock;
    };
}
