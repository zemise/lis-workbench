#include "regular_report_barcode_range.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace regular_barcode {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

int compare_digit_runs(const std::string& left, std::size_t lb, std::size_t le,
                       const std::string& right, std::size_t rb, std::size_t re) {
    while (lb < le && left[lb] == '0') ++lb;
    while (rb < re && right[rb] == '0') ++rb;
    const std::size_t llen = le - lb;
    const std::size_t rlen = re - rb;
    if (llen != rlen) return llen < rlen ? -1 : 1;
    for (std::size_t i = 0; i < llen; ++i) {
        if (left[lb + i] != right[rb + i])
            return left[lb + i] < right[rb + i] ? -1 : 1;
    }
    return 0;
}

std::string duplicate_key(const search::ReportRow& row) {
    const char sep = '\x1f';
    std::string date = trim(row.chk_date);
    if (date.size() > 10) date.resize(10);
    return trim(row.oper_no) + sep + trim(row.txm_no) + sep +
           trim(row.group_name) + sep + trim(row.name) + sep +
           trim(row.sample_name) + sep + trim(row.dept_name) + sep +
           trim(row.reg_no) + sep + date;
}

}  // namespace

int compare_sample_numbers(const std::string& leftValue,
                           const std::string& rightValue) {
    const std::string left = trim(leftValue);
    const std::string right = trim(rightValue);
    std::size_t li = 0;
    std::size_t ri = 0;
    while (li < left.size() && ri < right.size()) {
        const bool ld = std::isdigit(static_cast<unsigned char>(left[li])) != 0;
        const bool rd = std::isdigit(static_cast<unsigned char>(right[ri])) != 0;
        if (ld && rd) {
            std::size_t le = li;
            std::size_t re = ri;
            while (le < left.size() && std::isdigit(static_cast<unsigned char>(left[le]))) ++le;
            while (re < right.size() && std::isdigit(static_cast<unsigned char>(right[re]))) ++re;
            const int compared = compare_digit_runs(left, li, le, right, ri, re);
            if (compared != 0) return compared;
            li = le;
            ri = re;
            continue;
        }
        const unsigned char lc = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(left[li])));
        const unsigned char rc = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(right[ri])));
        if (lc != rc) return lc < rc ? -1 : 1;
        ++li;
        ++ri;
    }
    if (li != left.size()) return 1;
    if (ri != right.size()) return -1;
    return 0;
}

RangeSelection select_range(const std::vector<search::ReportRow>& rows,
                            const std::string& firstValue,
                            const std::string& lastValue) {
    RangeSelection result;
    const std::string first = trim(firstValue);
    const std::string last = trim(lastValue);
    if (first.empty() || last.empty()) {
        result.error = "start and end sample numbers are required";
        return result;
    }
    if (compare_sample_numbers(first, last) > 0) {
        result.error = "start sample number is greater than end sample number";
        return result;
    }

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const std::string sample = trim(row.oper_no);
        if (sample.empty() || compare_sample_numbers(sample, first) < 0 ||
            compare_sample_numbers(sample, last) > 0) {
            continue;
        }
        RangeCandidate candidate;
        candidate.source_index = index;
        candidate.printable = !trim(row.txm_no).empty();
        candidate.default_selected = candidate.printable;
        if (!candidate.printable) {
            candidate.reason = "barcode is empty";
            ++result.invalid_count;
        }
        result.candidates.push_back(std::move(candidate));
    }

    std::stable_sort(result.candidates.begin(), result.candidates.end(),
        [&rows](const RangeCandidate& left, const RangeCandidate& right) {
            const auto& lrow = rows[left.source_index];
            const auto& rrow = rows[right.source_index];
            const int compared = compare_sample_numbers(lrow.oper_no, rrow.oper_no);
            if (compared != 0) return compared < 0;
            return trim(lrow.id) < trim(rrow.id);
        });

    std::unordered_set<std::string> seen;
    for (auto& candidate : result.candidates) {
        if (!candidate.printable) continue;
        const std::string key = duplicate_key(rows[candidate.source_index]);
        if (!seen.insert(key).second) {
            candidate.duplicate = true;
            candidate.default_selected = false;
            candidate.reason = "possible duplicate";
            ++result.duplicate_count;
        }
    }
    return result;
}

}  // namespace regular_barcode
