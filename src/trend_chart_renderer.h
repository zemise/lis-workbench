#pragma once

#include "trend_core.h"

#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace search {

#ifdef _WIN32
void draw_trend_chart(HWND hwnd, HDC dc, const std::vector<const TrendPoint*>& points);
#endif

}  // namespace search
