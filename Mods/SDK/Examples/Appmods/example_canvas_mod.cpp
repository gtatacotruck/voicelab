
#include "../../VLabAppModAPI.h"
#include <cstring>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr int kCanvasId = 1;

    struct Star { float x, y, speed; unsigned char brightness; };
    std::vector<Star> g_stars;
    int g_lastW = 0, g_lastH = 0;

    float randf() { return (float) std::rand() / (float) RAND_MAX; }

    void seedStars (int w, int h, int count)
    {
        g_stars.clear();
        for (int i = 0; i < count; ++i)
            g_stars.push_back ({ randf() * (float) w, randf() * (float) h,
                                  0.3f + randf() * 1.5f, (unsigned char) (80 + std::rand() % 176) });
    }

    inline void putPixel (unsigned char* rgba, int w, int h, int strideBytes, int x, int y,
                           unsigned char r, unsigned char g, unsigned char b, unsigned char a)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        unsigned char* px = rgba + (size_t) y * (size_t) strideBytes + (size_t) x * 4;
        px[0] = r; px[1] = g; px[2] = b; px[3] = a;
    }
}

VLAB_APPMOD_EXPORT void vlab_appmod_get_info (VLabAppModInfo* outInfo)
{
    std::memset (outInfo, 0, sizeof (VLabAppModInfo));
    std::strncpy (outInfo->name, "Example Canvas Mod", sizeof (outInfo->name) - 1);
    std::strncpy (outInfo->author, "vlab", sizeof (outInfo->author) - 1);
    std::strncpy (outInfo->version, "1.0", sizeof (outInfo->version) - 1);
    std::strncpy (outInfo->description, "A full-view animated starfield, click to add stars, Space to reset.",
                  sizeof (outInfo->description) - 1);
    outInfo->sdkVersion = VLAB_APPMOD_SDK_VERSION;
}

VLAB_APPMOD_EXPORT bool vlab_appmod_init (const VLabAppHostAPI* host)
{
    return host->registerCanvas (kCanvasId, "Starfield", 800, 600, true);
}

VLAB_APPMOD_EXPORT void vlab_appmod_tick() {} // all animation state advances inside paint() itself

VLAB_APPMOD_EXPORT void vlab_appmod_on_action (int, bool) {} // no buttons/toggles/hotkeys registered

VLAB_APPMOD_EXPORT void vlab_appmod_canvas_resized (int id, int width, int height)
{
    if (id != kCanvasId) return;
    g_lastW = width; g_lastH = height;
    seedStars (width, height, 220);
}

VLAB_APPMOD_EXPORT void vlab_appmod_canvas_paint (int id, unsigned char* rgba, int width, int height, int strideBytes)
{
    if (id != kCanvasId) return;
    if (g_stars.empty() || g_lastW != width || g_lastH != height)
    {
        g_lastW = width; g_lastH = height;
        seedStars (width, height, 220);
    }

    for (int y = 0; y < height; ++y)
    {
        unsigned char* row = rgba + (size_t) y * (size_t) strideBytes;
        for (int x = 0; x < width; ++x)
        {
            row[x * 4 + 0] = 6; row[x * 4 + 1] = 8; row[x * 4 + 2] = 14; row[x * 4 + 3] = 255;
        }
    }

    for (auto& s : g_stars)
    {
        s.x -= s.speed;
        if (s.x < 0) { s.x = (float) width; s.y = randf() * (float) height; }
        putPixel (rgba, width, height, strideBytes, (int) s.x, (int) s.y, s.brightness, s.brightness, 255, 255);
    }
}

VLAB_APPMOD_EXPORT void vlab_appmod_canvas_mouse (int id, int eventType, int x, int y, int /*buttons*/)
{
    if (id != kCanvasId) return;
    if (eventType == 1) // mouse down, add a new bright star at the click point
        g_stars.push_back ({ (float) x, (float) y, 0.3f + randf() * 1.5f, 255 });
}

VLAB_APPMOD_EXPORT void vlab_appmod_canvas_key (int id, int keyCode, bool isDown, const char* /*textDescription*/)
{
    if (id != kCanvasId || ! isDown) return;
    if (keyCode == ' ') seedStars (g_lastW, g_lastH, 220); // Space, reset
}

VLAB_APPMOD_EXPORT void vlab_appmod_shutdown()
{
    g_stars.clear();
}
