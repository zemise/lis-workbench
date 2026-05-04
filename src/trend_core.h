#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "search_core.h"

#include <string>
#include <vector>

namespace search {

struct TrendPoint {
    std::string rep_no;
    std::string txm_no;
    std::string oper_no;
    std::string patient_name;
    std::string report_time;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string result_text;
    double result_value = 0.0;
    bool has_numeric_value = false;
    std::string unit;
    std::string lower_bound;
    std::string upper_bound;
    std::string normal;
};

struct TrendItemOption {
    std::string item_code;
    std::string item_name;
    std::string unit;
};

bool query_trend_points(const DbSettings& settings, const QueryInput& input, std::vector<TrendPoint>& rows, std::string& error);
std::vector<TrendItemOption> trend_item_options(const std::vector<TrendPoint>& rows);

}  // namespace search
