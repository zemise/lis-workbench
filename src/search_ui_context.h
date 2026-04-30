#pragma once

#ifdef _WIN32

#include "search_ui_layout.h"

#include <windows.h>

namespace search {

struct UiContext {
    MainUiHandles handles;
    HWND main_window = nullptr;
    HFONT ui_font = nullptr;
};

}  // namespace search

#endif
