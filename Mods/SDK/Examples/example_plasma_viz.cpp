#include "VLabVisualizerAPI.h"
#include <cstring>
#include <cmath>

static int gW = 0, gH = 0;
static float gPhase = 0.0f;

extern "C" VLAB_VIZ_EXPORT void vlab_viz_get_info(VLabVizInfo* info) {
    std::strncpy(info->name, "Example Plasma", sizeof(info->name));
    std::strncpy(info->author, "Test", sizeof(info->author));
    std::strncpy(info->version, "1.0", sizeof(info->version));
    info->sdkVersion = VLAB_VIZ_SDK_VERSION;
}
extern "C" VLAB_VIZ_EXPORT bool vlab_viz_init(int width, int height) { gW=width; gH=height; return true; }
extern "C" VLAB_VIZ_EXPORT void vlab_viz_resize(int width, int height) { gW=width; gH=height; }
extern "C" VLAB_VIZ_EXPORT void vlab_viz_render(const float* left, const float* right, int numSamples,
                                                 unsigned char* rgba, int width, int height) {
    float rms = 0.0f;
    for (int i = 0; i < numSamples; ++i) rms += left[i]*left[i];
    rms = numSamples > 0 ? std::sqrt(rms/numSamples) : 0.0f;
    gPhase += 0.05f + rms * 0.5f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = std::sin(x*0.05f+gPhase) + std::cos(y*0.05f-gPhase);
            unsigned char* p = rgba + (y*width+x)*4;
            p[0] = (unsigned char)(128+127*std::sin(v));
            p[1] = (unsigned char)(128+127*std::sin(v+2.0f));
            p[2] = (unsigned char)(128+127*std::sin(v+4.0f));
            p[3] = 255;
        }
    }
}
extern "C" VLAB_VIZ_EXPORT void vlab_viz_shutdown() {}
