#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <windows.h>

struct ModuleContext {
    HWND mdiClient = nullptr;
    HINSTANCE instance = nullptr;
    HFONT uiFont = nullptr;
    search::DbSettings dbSettings;
    int fontSize = 9;
    void* appContext = nullptr;  // optional pointer to app::Context for global state sync
};

struct ModuleDef {
    const wchar_t* name;          // module id + INI section name
    const wchar_t* menuParent;    // e.g. L"检验管理"
    const wchar_t* menuLabel;     // e.g. L"检验结果查询(&Q)..."
    int menuId;
    HWND (*create)(const ModuleContext& ctx);
};

#endif
