/*
    V LAB Lite Visualizer Mod SDK, v1
    -------------------------------------
    A second, separate mod type from the DLL audio mods documented in
    SDK.md / VLabModAPI.h: a visualizer mod draws a custom visualization
    into a pixel buffer instead of processing audio. It runs on the UI
    thread (not the audio thread) on a timer tick, nowhere near as
    strict as the audio mod's real-time constraints, but it still needs
    to be reasonably fast since it runs every frame (see 1.3 in SDK.md
    for the general real-time-safety mindset; the same "don't block"
    logic applies here even though the deadline is looser).

    Five required exports (extern "C", stable ABI, same reasoning as the
    audio mod SDK):

        void vlab_viz_get_info(VLabVizInfo* outInfo);
        bool vlab_viz_init(int width, int height);
        void vlab_viz_render(const float* left, const float* right, int numSamples,
                              unsigned char* rgba, int width, int height);
        void vlab_viz_resize(int width, int height);
        void vlab_viz_shutdown();

    Plus one OPTIONAL sixth export, if you want it:

        void vlab_viz_render_ex(const VLabVizFrame* frame, unsigned char* rgba,
                                 int width, int height);

    If your mod exports vlab_viz_render_ex, the host calls that INSTEAD of
    vlab_viz_render (still export vlab_viz_render too, as a fallback for
    anyone running an older vlab build that doesn't know about render_ex
    yet, see VLabVizFrame below). render_ex gets everything the plain
    render() gets, plus a precomputed frequency spectrum (so you don't
    have to run your own FFT), a frame-to-frame delta time (so animation
    speed doesn't depend on the host's tick rate), the sample rate (to
    turn spectrum bin index into Hz), and the app's current theme accent
    colour (so a mod can match whatever theme the user picked instead of
    hardcoding its own palette).

    Minimal example (a plasma-ish scrolling color field driven by the
    signal's RMS level, deliberately simple, not meant to be pretty):

        #include "VLabVisualizerAPI.h"
        #include <cstring>
        #include <cmath>

        static int gW = 0, gH = 0;
        static float gPhase = 0.0f;

        extern "C" VLAB_VIZ_EXPORT void vlab_viz_get_info(VLabVizInfo* info) {
            std::strncpy(info->name, "Example Plasma", sizeof(info->name));
            std::strncpy(info->author, "Your Name", sizeof(info->author));
            std::strncpy(info->version, "1.0", sizeof(info->version));
            info->sdkVersion = VLAB_VIZ_SDK_VERSION;
        }

        extern "C" VLAB_VIZ_EXPORT bool vlab_viz_init(int width, int height) {
            gW = width; gH = height;
            return true;
        }

        extern "C" VLAB_VIZ_EXPORT void vlab_viz_resize(int width, int height) {
            gW = width; gH = height;
        }

        extern "C" VLAB_VIZ_EXPORT void vlab_viz_render(const float* left, const float* right,
                                                          int numSamples, unsigned char* rgba,
                                                          int width, int height) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i) rms += left[i] * left[i];
            rms = numSamples > 0 ? std::sqrt(rms / numSamples) : 0.0f;
            gPhase += 0.05f + rms * 0.5f;

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float v = std::sin(x * 0.05f + gPhase) + std::cos(y * 0.05f - gPhase);
                    unsigned char* p = rgba + (y * width + x) * 4;
                    p[0] = (unsigned char)(128 + 127 * std::sin(v));
                    p[1] = (unsigned char)(128 + 127 * std::sin(v + 2.0f));
                    p[2] = (unsigned char)(128 + 127 * std::sin(v + 4.0f));
                    p[3] = 255;
                }
            }
        }

        extern "C" VLAB_VIZ_EXPORT void vlab_viz_shutdown() {}

    A second example, this one using render_ex for a theme-matched
    spectrum bar display driven by the host's FFT instead of raw samples
    (goes alongside the same vlab_viz_get_info/init/resize/shutdown/
    render above, render_ex is additive, not a replacement):

        extern "C" VLAB_VIZ_EXPORT void vlab_viz_render_ex(const VLabVizFrame* frame,
                                                             unsigned char* rgba,
                                                             int width, int height) {
            std::memset(rgba, 0, (size_t) width * height * 4);
            if (frame->numFftBins <= 0) return;

            const int bars = 48;
            for (int b = 0; b < bars; ++b) {
                const int bin = 1 + (int) ((double) b / bars * (frame->numFftBins - 1));
                const float mag = frame->fftMagnitudes[bin];
                float h = mag * height * 4.0f;
                if (h < 0.0f) h = 0.0f;
                if (h > height) h = (float) height;
                const int barHeight = (int) h;
                const int x0 = b * width / bars, x1 = (b + 1) * width / bars;
                for (int y = height - barHeight; y < height; ++y)
                    for (int x = x0; x < x1; ++x) {
                        unsigned char* p = rgba + (y * width + x) * 4;
                        p[0] = frame->accentR; p[1] = frame->accentG; p[2] = frame->accentB; p[3] = 255;
                    }
            }
        }

    Build the same way as an audio mod (see SDK.md section 1.7), drop the
    result into mods/visualizers, pick it from the Visualizer tab's
    "Visualizer" dropdown.
*/
#pragma once

#if defined(_WIN32)
  #define VLAB_VIZ_EXPORT __declspec(dllexport)
  #define VLAB_VIZ_CALL   __cdecl
#else
  #define VLAB_VIZ_EXPORT __attribute__((visibility("default")))
  #define VLAB_VIZ_CALL
#endif

#define VLAB_VIZ_SDK_VERSION 1

extern "C"
{
    struct VLabVizInfo
    {
        char name[64];
        char author[64];
        char version[16];
        int  sdkVersion; // must be set to VLAB_VIZ_SDK_VERSION
    };

    // Parameters for vlab_viz_render_ex export
    struct VLabVizFrame
    {
        const float* left;             // numSamples entries
        const float* right;            // numSamples entries
        int numSamples;

        const float* fftMagnitudes;    // numFftBins entries, linear magnitude (not dB), from the left channel
        int numFftBins;                // 0 if the host couldn't compute one this frame (e.g. not enough samples yet)

        float sampleRate;              // Hz, for turning a bin index into a frequency: bin * sampleRate / (2 * numFftBins)
        float deltaSeconds;            // time since the previous render call, for frame-rate-independent animation

        unsigned char accentR, accentG, accentB; // the app's current theme accent colour
    };

    typedef void (VLAB_VIZ_CALL *VLabVizGetInfoFn) (VLabVizInfo*);
    typedef bool (VLAB_VIZ_CALL *VLabVizInitFn)    (int, int);
    typedef void (VLAB_VIZ_CALL *VLabVizRenderFn)  (const float*, const float*, int, unsigned char*, int, int);
    typedef void (VLAB_VIZ_CALL *VLabVizRenderExFn)(const VLabVizFrame*, unsigned char*, int, int);
    typedef void (VLAB_VIZ_CALL *VLabVizResizeFn)  (int, int);
    typedef void (VLAB_VIZ_CALL *VLabVizShutdownFn)();
}
