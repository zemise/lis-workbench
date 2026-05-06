#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <windows.h>

namespace app {

struct Context {
    HINSTANCE instance = nullptr;
    HWND mainWindow = nullptr;
    HWND mdiClient = nullptr;
    HFONT uiFont = nullptr;
    HFONT menuFont = nullptr;
    search::DbSettings dbSettings;
    int fontSize = 9;
};

// Module registration — add new modules here
struct ModuleDef {
    const wchar_t* menuLabel;
    void (*onClick)(HWND owner, const Context& ctx);
};

}  // namespace app

#endif
