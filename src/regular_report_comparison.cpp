#include "regular_report_comparison.h"

#include <unordered_map>
#include <utility>

namespace regular_comparison {

bool ViewCache::select(const CacheKey& next) {
    if (key && *key == next) return false;
    invalidate();
    key = next;
    return true;
}

void ViewCache::invalidate() {
    key.reset();
    current = LoadPhase::Empty;
    comparison = LoadPhase::Empty;
}

std::vector<Item> merge_items(const std::array<std::vector<search::ResultRow>, kReportCount>& reports) {
    std::vector<Item> items;
    std::unordered_map<std::string, std::size_t> indexes;
    for (std::size_t col = 0; col < reports.size(); ++col) {
        for (const auto& result : reports[col]) {
            // Empty codes must not combine unrelated items by their display names.
            if (result.item_code.empty()) {
                Item item;
                item.reference = result;
                item.results[col] = result;
                items.push_back(std::move(item));
                continue;
            }
            auto entry = indexes.emplace(result.item_code, items.size());
            if (entry.second) {
                Item item;
                item.reference = result;
                items.push_back(std::move(item));
            }
            auto& cell = items[entry.first->second].results[col];
            // Match existing detail order when a report contains duplicate codes.
            if (!cell) cell = result;
        }
    }
    return items;
}
}  // namespace regular_comparison
