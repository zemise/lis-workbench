#pragma once
#include "search_core.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace regular_comparison {

inline constexpr std::size_t kReportCount = 4;

// Key fields: connection, REP_NO, REG_NO, ROOM_CODE, MACH_CODE, CHK_DATE.
using CacheKey = std::array<std::string, 6>;
enum class LoadPhase { Empty, Loading, Ready, Failed };
struct ViewCache {
    std::optional<CacheKey> key;
    LoadPhase current = LoadPhase::Empty;
    LoadPhase comparison = LoadPhase::Empty;
    bool select(const CacheKey& next);
    void invalidate();
};

struct Item {
    search::ResultRow reference;
    std::array<std::optional<search::ResultRow>, kReportCount> results;
};
// Current report order first, then unseen history items in newest-report order.
std::vector<Item> merge_items(const std::array<std::vector<search::ResultRow>, kReportCount>& reports);
}  // namespace regular_comparison
