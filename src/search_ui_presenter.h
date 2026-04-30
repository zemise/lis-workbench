#pragma once

#ifdef _WIN32

#include "search_core.h"
#include "search_ui_layout.h"

#include <string>
#include <vector>

namespace search {

void set_status_text(const MainUiHandles& ui, const std::wstring& text);
void clear_result_lists(const MainUiHandles& ui);
void present_report_rows(const MainUiHandles& ui, const std::vector<ReportRow>& rows);
void present_result_rows(const MainUiHandles& ui, const std::vector<ResultRow>& rows);

}  // namespace search

#endif
