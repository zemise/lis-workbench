#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <windows.h>

namespace app {

constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 20;
constexpr UINT WM_APP_FONT_CHANGED = WM_APP + 21;

struct Context {
    HINSTANCE instance = nullptr;
    HWND mainWindow = nullptr;
    HWND mdiClient = nullptr;
    HFONT uiFont = nullptr;
    HFONT menuFont = nullptr;
    search::DbSettings dbSettings;
    int fontSize = 9;
};

}  // namespace app

#endif
