#pragma once

#ifdef _WIN32

#include "search_core.h"

#include <functional>

#include <windows.h>

namespace search {

struct CommandEventHandlers {
    std::function<void()> on_room_changed;
    std::function<void()> on_query;
    std::function<void(HWND)> on_show_settings;
    std::function<void(HWND)> on_unimplemented_action;
    std::function<void(HWND)> on_exit;
};

struct NotifyEventHandlers {
    std::function<void(int)> on_report_selected;
    std::function<COLORREF(const ReportRow&)> report_row_background;
    std::function<COLORREF(const ResultRow&)> result_row_color;
    const std::vector<ReportRow>* report_rows = nullptr;
    const std::vector<ResultRow>* result_rows = nullptr;
};

bool handle_command(HWND hwnd, WPARAM wparam, const CommandEventHandlers& handlers);
bool handle_notify(LPARAM lparam, const NotifyEventHandlers& handlers, LRESULT& result);

}  // namespace search

#endif
