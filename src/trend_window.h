#pragma once

#ifdef _WIN32

#include "app_settings.h"
#include "search_app.h"

#include <windows.h>

namespace search {

void show_trend_window(HWND owner, HFONT font, const DbSettings& settings, const QueryInput& input);

}  // namespace search

#endif
