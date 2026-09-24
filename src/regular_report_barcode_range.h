#pragma once

#include "search_core.h"

#include <cstddef>
#include <string>
#include <vector>

namespace regular_barcode {

struct RangeCandidate {
    std::size_t source_index = 0;
    bool printable = false;
    bool default_selected = false;
    bool duplicate = false;
    std::string reason;
};

struct RangeSelection {
    std::string error;
    std::vector<RangeCandidate> candidates;
    std::size_t invalid_count = 0;
    std::size_t duplicate_count = 0;
};

int compare_sample_numbers(const std::string& left, const std::string& right);
RangeSelection select_range(const std::vector<search::ReportRow>& rows,
                            const std::string& first,
                            const std::string& last);

}  // namespace regular_barcode
