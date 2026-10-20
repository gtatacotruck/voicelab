#pragma once
#include "SDK/VLabVisualizerAPI.h"
#include "../DSP/Common.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

namespace vlab
{
    struct LoadedVisualizer
    {
        juce::String fileName;
        VLabVizInfo info {};
        std::unique_ptr<juce::DynamicLibrary> lib;
        VLabVizRenderFn renderFn = nullptr;
        VLabVizRenderExFn renderExFn = nullptr; // optional, see VLabVisualizerAPI.h
        VLabVizResizeFn resizeFn = nullptr;
        VLabVizShutdownFn shutdownFn = nullptr;
        bool initOk = false;
        int lastW = 0, lastH = 0;
        double lastRenderMs = 0.0; // for VLabVizFrame::deltaSeconds
    };

    class VisualizerModHost
    {
    public:
        ~VisualizerModHost() { unloadAll(); }

        void scanAndLoad (const juce::File& folder, int initialW, int initialH,
                           const std::function<void (int, int)>& onProgress = {})
        {
            if (! folder.isDirectory())
                return;
            auto files = findVizFiles (folder);
            const int total = files.size();
            int done = 0;
            for (auto& f : files)
            {
                bool already = std::any_of (mods.begin(), mods.end(),
                                             [&] (auto& m) { return m->fileName == f.getFileName(); });
                if (! already)
                {
                    juce::String err;
                    if (auto mod = loadOne (f, initialW, initialH, err))
                        mods.push_back (std::move (mod));
                    else if (err.isNotEmpty())
                        lastError = err;
                }
                ++done;
                if (onProgress) onProgress (done, total);
            }
        }

        static std::vector<std::unique_ptr<LoadedVisualizer>> scanDetached (
            const juce::File& folder, int initialW, int initialH,
            const std::function<void (int, int)>& onProgress = {},
            int cooldownMs = 0)
        {
            std::vector<std::unique_ptr<LoadedVisualizer>> result;
            if (! folder.isDirectory())
                return result;
            auto files = findVizFiles (folder);
            const int total = files.size();
            int done = 0;
            for (auto& f : files)
            {
                juce::String err;
                if (auto mod = loadOne (f, initialW, initialH, err))
                    result.push_back (std::move (mod));
                ++done;
                if (onProgress) onProgress (done, total);

                if (cooldownMs > 0 && done < total)
                    juce::Thread::sleep (cooldownMs);
            }
            return result;
        }

        // Message-thread-only.
        void adopt (std::vector<std::unique_ptr<LoadedVisualizer>>&& loaded)
        {
            for (auto& m : mods)
                if (m->shutdownFn) m->shutdownFn();
            mods = std::move (loaded);
        }

        void tryLoad (const juce::File& file, int initialW, int initialH)
        {
            juce::String err;
            if (auto mod = loadOne (file, initialW, initialH, err))
                mods.push_back (std::move (mod));
            else if (err.isNotEmpty())
                lastError = err;
        }

        void unloadAll()
        {
            for (auto& m : mods)
                if (m->shutdownFn) m->shutdownFn();
            mods.clear();
        }

        std::vector<std::unique_ptr<LoadedVisualizer>>& getMods() { return mods; }

        juce::Image render (int index, const std::vector<float>& left, const std::vector<float>& right,
                             int width, int height, juce::Colour accentColour = juce::Colours::white)
        {
            if (index < 0 || index >= (int) mods.size())
                return {};
            auto& mod = *mods[(size_t) index];
            if (! mod.initOk)
                return {};

            if (mod.lastW != width || mod.lastH != height)
            {
                mod.resizeFn (width, height);
                mod.lastW = width;
                mod.lastH = height;
            }

            pixelBuffer.assign ((size_t) width * height * 4, 0);
            const int n = juce::jmin ((int) left.size(), (int) right.size());

            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            const float deltaSeconds = mod.lastRenderMs > 0.0 ? (float) ((nowMs - mod.lastRenderMs) / 1000.0) : 0.0f;
            mod.lastRenderMs = nowMs;

            if (mod.renderExFn != nullptr)
            {
                const int numBins = computeSpectrum (left, n);

                VLabVizFrame frame {};
                frame.left = n > 0 ? left.data() : nullptr;
                frame.right = n > 0 ? right.data() : nullptr;
                frame.numSamples = n;
                frame.fftMagnitudes = numBins > 0 ? fftMagnitudes.data() : nullptr;
                frame.numFftBins = numBins;
                frame.sampleRate = (float) vlab::kSampleRate;
                frame.deltaSeconds = deltaSeconds;
                frame.accentR = (unsigned char) accentColour.getRed();
                frame.accentG = (unsigned char) accentColour.getGreen();
                frame.accentB = (unsigned char) accentColour.getBlue();

                mod.renderExFn (&frame, pixelBuffer.data(), width, height);
            }
            else
            {
                mod.renderFn (n > 0 ? left.data() : nullptr, n > 0 ? right.data() : nullptr, n,
                              pixelBuffer.data(), width, height);
            }

            juce::Image img (juce::Image::ARGB, width, height, false);
            juce::Image::BitmapData bmp (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const unsigned char* p = &pixelBuffer[(size_t) (y * width + x) * 4];
                    bmp.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2], p[3]));
                }
            }
            return img;
        }

        juce::String lastError;

    private:
        int computeSpectrum (const std::vector<float>& samples, int numSamples)
        {
            juce::ignoreUnused (numSamples);
            if ((int) samples.size() < fftSize)
                return 0;

            if (fftWindow.empty())
            {
                fftWindow.resize (fftSize);
                for (int i = 0; i < fftSize; ++i)
                    fftWindow[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
            }

            fftBuf.assign ((size_t) fftSize * 2, 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                const int srcIdx = (int) samples.size() - fftSize + i; // most recent fftSize samples
                fftBuf[(size_t) i] = samples[(size_t) srcIdx] * fftWindow[(size_t) i];
            }

            fft.performRealOnlyForwardTransform (fftBuf.data());

            const int numBins = fftSize / 2;
            fftMagnitudes.resize ((size_t) numBins);
            for (int b = 0; b < numBins; ++b)
            {
                const float re = fftBuf[(size_t) b * 2], im = fftBuf[(size_t) b * 2 + 1];
                fftMagnitudes[(size_t) b] = std::sqrt (re * re + im * im) / (float) fftSize;
            }
            return numBins;
        }

        static juce::Array<juce::File> findVizFiles (const juce::File& folder)
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

        static std::unique_ptr<LoadedVisualizer> loadOne (const juce::File& file, int initialW, int initialH,
                                                            juce::String& outError)
        {
            auto mod = std::make_unique<LoadedVisualizer>();
            mod->fileName = file.getFileName();
            mod->lib = std::make_unique<juce::DynamicLibrary>();
            if (! mod->lib->open (file.getFullPathName()))
            {
                outError = "Could not open " + file.getFileName();
                return nullptr;
            }

            auto getInfoFn = (VLabVizGetInfoFn) mod->lib->getFunction ("vlab_viz_get_info");
            auto initFn    = (VLabVizInitFn)    mod->lib->getFunction ("vlab_viz_init");
            mod->renderFn   = (VLabVizRenderFn)  mod->lib->getFunction ("vlab_viz_render");
            mod->renderExFn = (VLabVizRenderExFn) mod->lib->getFunction ("vlab_viz_render_ex"); // optional
            mod->resizeFn   = (VLabVizResizeFn)  mod->lib->getFunction ("vlab_viz_resize");
            mod->shutdownFn = (VLabVizShutdownFn) mod->lib->getFunction ("vlab_viz_shutdown");

            if (getInfoFn == nullptr || initFn == nullptr || mod->renderFn == nullptr
                || mod->resizeFn == nullptr || mod->shutdownFn == nullptr)
            {
                outError = file.getFileName() + " is missing required exports (see VLabVisualizerAPI.h)";
                return nullptr;
            }

            getInfoFn (&mod->info);
            if (mod->info.sdkVersion < 1)
            {
                outError = file.getFileName() + " reports an invalid visualizer SDK version";
                return nullptr;
            }

            mod->initOk = initFn (initialW, initialH);
            mod->lastW = initialW;
            mod->lastH = initialH;
            return mod;
        }

        std::vector<std::unique_ptr<LoadedVisualizer>> mods;
        std::vector<unsigned char> pixelBuffer;

        static constexpr int fftOrder = 10, fftSize = 1 << fftOrder; // 1024
        juce::dsp::FFT fft { fftOrder };
        std::vector<float> fftWindow, fftBuf, fftMagnitudes;
    };
}
