#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <functional>

#include <windows.h>

namespace search {

struct SettingsDialogCallbacks {
    std::function<void(HWND, const DbSettings&)> on_test;
    std::function<void(const DbSettings&, int)> on_save;
};

void show_settings_dialog(HWND owner,
                          HFONT font,
                          const DbSettings& settings,
                          int font_size,
                          const SettingsDialogCallbacks& callbacks);

}  // namespace search

#endif
