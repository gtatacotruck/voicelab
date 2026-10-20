/*
    V LAB Lite Mod SDK - v1
    ------------------------
    A DLL/.so/.dylib mod is a native shared library that exports the six
    functions declared below (extern "C", so the ABI is stable across
    compilers). VLabModHost loads it, calls vlab_mod_get_info() once to get
    a name/author/version, calls vlab_mod_init() before first use, then
    calls vlab_mod_process() once per audio block for as long as the mod
    is enabled, and vlab_mod_shutdown() when the app closes or the mod is
    unloaded.

    IMPORTANT - READ BEFORE WRITING OR INSTALLING A MOD:
    A native DLL mod is NOT sandboxed. Once loaded, its code runs with the
    exact same privileges as the rest of the app - it can read/write files,
    open network connections, etc. This SDK does not attempt to restrict
    that (doing so reliably would require a real sandboxing layer, which
    is future work, not something bolted on here). The narrow six-function
    API just means a mod *can't accidentally* do more than process audio if
    it's well-behaved - it does not mean a malicious mod is prevented from
    doing more. Only load mods from people/sources you trust, the same way
    you would with a VST plugin or any other native plugin format.

    Minimal example implementation (a gain mod):

        #include "VLabModAPI.h"
        #include <cstring>

        extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info(VLabModInfo* info) {
            std::strncpy(info->name, "Example Gain", sizeof(info->name));
            std::strncpy(info->author, "Your Name", sizeof(info->author));
            std::strncpy(info->version, "1.0", sizeof(info->version));
            info->sdkVersion = VLAB_MOD_SDK_VERSION;
        }

        extern "C" VLAB_MOD_EXPORT bool vlab_mod_init(double sampleRate, int maxBlockSize) {
            return true; // nothing to allocate for this simple example
        }

        extern "C" VLAB_MOD_EXPORT void vlab_mod_process(float* interleaved, int numFrames, int numChannels) {
            for (int i = 0; i < numFrames * numChannels; ++i) interleaved[i] *= 0.5f;
        }

        extern "C" VLAB_MOD_EXPORT void vlab_mod_set_param(int index, float value) {}
        extern "C" VLAB_MOD_EXPORT float vlab_mod_get_param(int index) { return 0.0f; }
        extern "C" VLAB_MOD_EXPORT void vlab_mod_shutdown() {}

    Build as a shared library (CMake: add_library(mymod SHARED mymod.cpp),
    or `g++ -shared -fPIC` / `cl /LD` directly) and drop the resulting
    .so/.dll/.dylib into the mods/dll folder shown in the Mods tab.

    ---- OPTIONAL: parameter metadata for automatic FX Rack sliders ----
    A mod with no metadata exports still loads and runs fine (it just gets
    an enable toggle and no sliders). Export these two extra functions and
    the host builds a slider in the FX Rack for each parameter automatically:

        extern "C" VLAB_MOD_EXPORT int vlab_mod_get_param_count() {
            return 1; // however many parameters this mod has
        }

        extern "C" VLAB_MOD_EXPORT void vlab_mod_get_param_info(int index, VLabModParamInfo* outInfo) {
            if (index == 0) {
                std::strncpy(outInfo->name, "Gain", sizeof(outInfo->name));
                outInfo->minValue = 0.0f;
                outInfo->maxValue = 2.0f;
                outInfo->defaultValue = 1.0f;
            }
        }

    The host calls vlab_mod_get_param_count() once right after a successful
    vlab_mod_init(), then vlab_mod_get_param_info() for each index in
    [0, count) to build the sliders, and from then on calls your existing
    vlab_mod_set_param(index, value) whenever the user moves one. Both
    functions are checked for existence independently - you can export
    just get_param_count with count returning 0 (no metadata, same as not
    exporting it at all), or export both. This is fully additive: mods
    built before this was added still load and run exactly as before,
    just without automatic sliders.
*/
#pragma once

#if defined(_WIN32)
  #define VLAB_MOD_EXPORT __declspec(dllexport)
  #define VLAB_MOD_CALL   __cdecl
#else
  #define VLAB_MOD_EXPORT __attribute__((visibility("default")))
  #define VLAB_MOD_CALL
#endif

#define VLAB_MOD_SDK_VERSION 1

extern "C"
{
    struct VLabModInfo
    {
        char name[64];
        char author[64];
        char version[16];
        int  sdkVersion; // must be set to VLAB_MOD_SDK_VERSION
    };

    struct VLabModParamInfo
    {
        char name[32];
        float minValue;
        float maxValue;
        float defaultValue;
    };

    typedef void (VLAB_MOD_CALL *VLabModGetInfoFn)      (VLabModInfo*);
    typedef bool (VLAB_MOD_CALL *VLabModInitFn)         (double, int);
    typedef void (VLAB_MOD_CALL *VLabModProcessFn)      (float*, int, int);
    typedef void (VLAB_MOD_CALL *VLabModSetParamFn)     (int, float);
    typedef float(VLAB_MOD_CALL *VLabModGetParamFn)     (int);
    typedef void (VLAB_MOD_CALL *VLabModShutdownFn)     ();
    typedef int  (VLAB_MOD_CALL *VLabModGetParamCountFn)();
    typedef void (VLAB_MOD_CALL *VLabModGetParamInfoFn) (int, VLabModParamInfo*);
}
