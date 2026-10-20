#include "VLabModAPI.h"
#include <cstring>
#include <atomic>

static std::atomic<float> gGain { 1.0f };

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info(VLabModInfo* info) {
    std::strncpy(info->name, "Adjustable Gain", sizeof(info->name));
    std::strncpy(info->author, "Test", sizeof(info->author));
    std::strncpy(info->version, "1.0", sizeof(info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}
extern "C" VLAB_MOD_EXPORT bool vlab_mod_init(double, int) { return true; }
extern "C" VLAB_MOD_EXPORT void vlab_mod_process(float* interleaved, int numFrames, int numChannels) {
    const float g = gGain.load();
    for (int i = 0; i < numFrames * numChannels; ++i) interleaved[i] *= g;
}
extern "C" VLAB_MOD_EXPORT void vlab_mod_set_param(int index, float value) {
    if (index == 0) gGain.store(value);
}
extern "C" VLAB_MOD_EXPORT float vlab_mod_get_param(int index) {
    return index == 0 ? gGain.load() : 0.0f;
}
extern "C" VLAB_MOD_EXPORT void vlab_mod_shutdown() {}

extern "C" VLAB_MOD_EXPORT int vlab_mod_get_param_count() { return 1; }
extern "C" VLAB_MOD_EXPORT void vlab_mod_get_param_info(int index, VLabModParamInfo* outInfo) {
    if (index == 0) {
        std::strncpy(outInfo->name, "Gain", sizeof(outInfo->name));
        outInfo->minValue = 0.0f;
        outInfo->maxValue = 2.0f;
        outInfo->defaultValue = 1.0f;
    }
}
