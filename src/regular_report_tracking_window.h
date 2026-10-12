#pragma once
#ifdef _WIN32
#include <windows.h>
#include "search_core.h"

void regularShowTrackingWindow(HWND owner, HFONT font, const std::string& connection,
                               const search::ReportRow& selected,
                               const std::string& originRoom, const std::string& originMachine);
#endif
