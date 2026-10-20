#pragma once


#if defined(_WIN32)
  #define VLAB_APPMOD_EXPORT extern "C" __declspec(dllexport)
  #define VLAB_APPMOD_CALL __cdecl
#else
  #define VLAB_APPMOD_EXPORT extern "C" __attribute__((visibility("default")))
  #define VLAB_APPMOD_CALL
#endif

#define VLAB_APPMOD_SDK_VERSION 1

#define VLAB_APPMOD_MAX_ACTIONS 32

#define VLAB_APPMOD_MAX_CANVASES 4

#define VLAB_APPMOD_MAX_SLIDERS 16

extern "C"
{
    struct VLabAppModInfo
    {
        char name[64];
        char author[64];
        char version[16];
        char description[128]; // one line, shown under the mod's name in the Mods tab
        int  sdkVersion;       // must be set to VLAB_APPMOD_SDK_VERSION
    };

    struct VLabAppHostAPI
    {
        // ---- info ----
        void (VLAB_APPMOD_CALL *getAppVersion)     (char* outBuf, int outBufLen); // e.g. "1.9.5"
        void (VLAB_APPMOD_CALL *getActiveTabName)  (char* outBuf, int outBufLen); // "Board"/"FxRack"/"Settings"/
        void (VLAB_APPMOD_CALL *checkForUpdates) (); // re-runs vlab's own version.json check on demand
        void (VLAB_APPMOD_CALL *switchTab)       (const char* tabName); // same names as getActiveTabName
        void (VLAB_APPMOD_CALL *showMessage)     (const char* text);    // brief on-screen notice from vlab itself
        void (VLAB_APPMOD_CALL *quitApp)         ();

        bool (VLAB_APPMOD_CALL *runCommand) (const char* name, const char* argJson,
                                              char* outResult, int outResultLen);


        bool (VLAB_APPMOD_CALL *registerButton) (int id, const char* label, const char* tooltip);

        bool (VLAB_APPMOD_CALL *registerToggle) (int id, const char* label, const char* tooltip, bool initialState);

        bool (VLAB_APPMOD_CALL *registerHotkey) (int id, const char* keyDescription, const char* label);

        void (VLAB_APPMOD_CALL *setToggleState) (int id, bool state);

        bool (VLAB_APPMOD_CALL *registerCanvas) (int id, const char* title,
                                                   int preferredW, int preferredH,
                                                   bool takeOverMainView);


        bool (VLAB_APPMOD_CALL *registerSlider) (int id, const char* label, const char* tooltip,
                                                   float minValue, float maxValue, float stepSize,
                                                   float initialValue);

        void (VLAB_APPMOD_CALL *setSliderValue) (int id, float value);

        bool (VLAB_APPMOD_CALL *getModDataString) (const char* key, char* outBuf, int outBufLen);
        void (VLAB_APPMOD_CALL *setModDataString) (const char* key, const char* value);

        bool (VLAB_APPMOD_CALL *isAdvancedMode) ();

        void (VLAB_APPMOD_CALL *reloadAllMods) ();

        void* hostUserData; // opaque, reserved for future use, currently always null
    };


    typedef void (VLAB_APPMOD_CALL *VLabAppModGetInfoFn)      (VLabAppModInfo* outInfo);
    typedef bool (VLAB_APPMOD_CALL *VLabAppModInitFn)         (const VLabAppHostAPI* host);
    typedef void (VLAB_APPMOD_CALL *VLabAppModTickFn)         ();
    typedef void (VLAB_APPMOD_CALL *VLabAppModOnActionFn)     (int id, bool toggleState);
    typedef void (VLAB_APPMOD_CALL *VLabAppModOnSliderFn)     (int id, float value);
    typedef void (VLAB_APPMOD_CALL *VLabAppModShutdownFn)     ();
    typedef void (VLAB_APPMOD_CALL *VLabAppModCanvasPaintFn)  (int id, unsigned char* rgba, int width, int height, int strideBytes);
    typedef void (VLAB_APPMOD_CALL *VLabAppModCanvasResizeFn) (int id, int width, int height);
    typedef void (VLAB_APPMOD_CALL *VLabAppModCanvasMouseFn)  (int id, int eventType, int x, int y, int buttons);
    typedef void (VLAB_APPMOD_CALL *VLabAppModCanvasKeyFn)    (int id, int keyCode, bool isDown, const char* textDescription);
}
