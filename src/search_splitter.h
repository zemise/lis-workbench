#pragma once

#ifdef _WIN32

#include <windows.h>

namespace search {

constexpr UINT WM_SPLITTER_DRAG = WM_APP + 90;
constexpr UINT WM_SPLITTER_RELEASED = WM_APP + 91;

void register_splitter_class(HINSTANCE instance);
HWND create_splitter(HWND parent, int id, int x, int y, int w, int h, HINSTANCE instance);

}  // namespace search

#endif
