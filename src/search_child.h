#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <windows.h>

HWND create_search_child(HWND mdiClient, HINSTANCE inst, HFONT font,
                         const search::DbSettings& dbSettings, int initFontSize);

#endif
