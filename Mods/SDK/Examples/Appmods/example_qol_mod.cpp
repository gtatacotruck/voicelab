
#include "../../VLabAppModAPI.h"
#include <cstring>

namespace
{
    const VLabAppHostAPI* g_host = nullptr;

    enum ActionId { kJumpToBoardButton = 1, kAnnounceToggle = 2, kHotkeyAction = 3 };

    void announce()
    {
        if (g_host && g_host->showMessage)
            g_host->showMessage ("Example QOL Mod says hi!");
    }
}

VLAB_APPMOD_EXPORT void vlab_appmod_get_info (VLabAppModInfo* outInfo)
{
    std::memset (outInfo, 0, sizeof (VLabAppModInfo));
    std::strncpy (outInfo->name, "Example QOL Mod", sizeof (outInfo->name) - 1);
    std::strncpy (outInfo->author, "vlab", sizeof (outInfo->author) - 1);
    std::strncpy (outInfo->version, "1.0", sizeof (outInfo->version) - 1);
    std::strncpy (outInfo->description, "Jump-to-Board button, an announce toggle, and an F9 hotkey.",
                  sizeof (outInfo->description) - 1);
    outInfo->sdkVersion = VLAB_APPMOD_SDK_VERSION;
}

VLAB_APPMOD_EXPORT bool vlab_appmod_init (const VLabAppHostAPI* host)
{
    g_host = host;

    host->registerButton (kJumpToBoardButton, "Jump to Board", "Switches vlab to the Board tab");
    host->registerToggle (kAnnounceToggle, "Announce on Startup", "Purely cosmetic, flips a switch and says hi", false);
    host->registerHotkey (kHotkeyAction, "f9", "Say hi (F9)");

    return true;
}

VLAB_APPMOD_EXPORT void vlab_appmod_tick() {} // nothing to poll, this mod is entirely event-driven

VLAB_APPMOD_EXPORT void vlab_appmod_on_action (int id, bool toggleState)
{
    switch (id)
    {
        case kJumpToBoardButton:
            if (g_host && g_host->switchTab) g_host->switchTab ("Board");
            break;
        case kAnnounceToggle:
            if (toggleState) announce();
            break;
        case kHotkeyAction:
            announce();
            break;
        default:
            break;
    }
}

VLAB_APPMOD_EXPORT void vlab_appmod_shutdown() { g_host = nullptr; }
