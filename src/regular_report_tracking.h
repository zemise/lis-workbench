#pragma once

#include "search_core.h"

namespace regular_tracking {

inline constexpr int kMinDays = 1;
inline constexpr int kMaxDays = 9999;
inline constexpr int kDefaultDays = 30;

struct Options {
    std::string connection;
    std::string patient;
    std::string origin_room;
    std::string origin_machine;
    std::string today;  // Local system date, YYYY-MM-DD; independent of selected report.
    int days = kDefaultDays;
    bool current_machine = false;
    bool reviewed = false;
};

// SQL loads the full date-range snapshot; checkbox state does not restrict the query.
bool build_filters(const Options& options, search::QueryFilters& filters, std::string& error);
// Checkbox filters operate on the complete date-range snapshot, never SQL.
std::vector<search::ReportRow> filter_reports(const std::vector<search::ReportRow>& rows,
                                            const Options& options);

}  // namespace regular_tracking
