#include "VLabModAPI.h"
#include <cstring>
#include <cstdio>

extern "C" VLAB_MOD_EXPORT void vlab_mod_get_info(VLabModInfo* info) {
    std::strncpy(info->name, "Example Gain", sizeof(info->name));
    std::strncpy(info->author, "Test", sizeof(info->author));
    std::strncpy(info->version, "1.0", sizeof(info->version));
    info->sdkVersion = VLAB_MOD_SDK_VERSION;
}

extern "C" VLAB_MOD_EXPORT bool vlab_mod_init(double sampleRate, int maxBlockSize) {
    std::printf("mod init: sr=%f block=%d\n", sampleRate, maxBlockSize);
    return true;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_process(float* interleaved, int numFrames, int numChannels) {
    for (int i = 0; i < numFrames * numChannels; ++i) interleaved[i] *= 0.5f;
}

extern "C" VLAB_MOD_EXPORT void vlab_mod_set_param(int, float) {}
extern "C" VLAB_MOD_EXPORT float vlab_mod_get_param(int) { return 0.0f; }
extern "C" VLAB_MOD_EXPORT void vlab_mod_shutdown() { std::printf("mod shutdown\n"); }
