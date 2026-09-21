#include "scheduled_result_check_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>

namespace scheduled_check {
namespace {

std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::string fingerprint(const Match &match) {
  std::ostringstream out;
  out << match.rule_id << '|' << match.rep_no << '|' << match.left_entry_id
      << '|' << match.right_entry_id << '|' << std::setprecision(17)
      << match.left_value << '|' << match.right_value;
  return out.str();
}

bool newer_entry_id(const std::string &candidate, const std::string &current) {
  std::string a = trim(candidate), b = trim(current);
  const auto az = a.find_first_not_of('0'), bz = b.find_first_not_of('0');
  a = az == std::string::npos ? "0" : a.substr(az);
  b = bz == std::string::npos ? "0" : b.substr(bz);
  if (a.size() != b.size())
    return a.size() > b.size();
  return a > b;
}

} // namespace

bool parse_number(const std::string &text, double &value) {
  const std::string clean = trim(text);
  if (clean.empty())
    return false;
  char *end = nullptr;
  const double parsed = std::strtod(clean.c_str(), &end);
  if (end == clean.c_str() || *end != '\0' || !std::isfinite(parsed))
    return false;
  value = parsed;
  return true;
}

bool compare_numbers(double left, const std::string &op, double right) {
  const double tolerance =
      (std::max)(1e-9, (std::max)(std::fabs(left), std::fabs(right)) * 1e-9);
  const bool equal = std::fabs(left - right) <= tolerance;
  if (op == ">")
    return left > right && !equal;
  if (op == ">=")
    return left > right || equal;
  if (op == "<")
    return left < right && !equal;
  if (op == "<=")
    return left < right || equal;
  if (op == "=")
    return equal;
  if (op == "!=")
    return !equal;
  return false;
}

std::vector<Match> evaluate(const std::vector<Rule> &rules,
                            const std::vector<ResultRow> &rows,
                            int *skipped_non_numeric) {
  if (skipped_non_numeric)
    *skipped_non_numeric = 0;
  std::map<std::string, std::map<std::string, ResultRow>> reports;
  for (const auto &row : rows) {
    auto &selected = reports[row.rep_no][row.item_code];
    const bool selectedEmpty = trim(selected.result).empty();
    const bool candidateEmpty = trim(row.result).empty();
    const bool replace = selected.entry_id.empty() ||
                         (selectedEmpty && !candidateEmpty) ||
                         (selectedEmpty == candidateEmpty &&
                          newer_entry_id(row.entry_id, selected.entry_id));
    if (replace)
      selected = row;
  }

  std::vector<Match> matches;
  for (const auto &rule : rules) {
    if (!rule.enabled || rule.left_item_code.empty())
      continue;
    double threshold = 0.0;
    if (rule.compare_with_value) {
      if (!parse_number(rule.right_value_text, threshold))
        continue;
    } else if (rule.right_item_code.empty()) {
      continue;
    }
    for (const auto &report : reports) {
      const auto leftIt = report.second.find(rule.left_item_code);
      if (leftIt == report.second.end())
        continue;
      double leftValue = 0.0, rightValue = threshold;
      if (!parse_number(leftIt->second.result, leftValue)) {
        if (skipped_non_numeric)
          ++*skipped_non_numeric;
        continue;
      }
      const auto rightIt = rule.compare_with_value
                               ? report.second.end()
                               : report.second.find(rule.right_item_code);
      if (!rule.compare_with_value) {
        if (rightIt == report.second.end())
          continue;
        if (!parse_number(rightIt->second.result, rightValue)) {
          if (skipped_non_numeric)
            ++*skipped_non_numeric;
          continue;
        }
      }
      if (!compare_numbers(leftValue, rule.op, rightValue))
        continue;
      Match match;
      match.rule_id = rule.id;
      match.rule_name = rule.name;
      match.rep_no = report.first;
      match.oper_no = leftIt->second.oper_no;
      match.room_code = leftIt->second.room_code;
      match.mach_code = leftIt->second.mach_code;
      match.mach_name = leftIt->second.mach_name;
      match.inspect_date = leftIt->second.inspect_date;
      match.left_entry_id = leftIt->second.entry_id;
      match.left_item_code = rule.left_item_code;
      match.left_item_name = rule.left_item_name.empty()
                                 ? leftIt->second.item_name
                                 : rule.left_item_name;
      match.left_result_text = trim(leftIt->second.result);
      match.left_value = leftValue;
      match.op = rule.op;
      match.compare_with_value = rule.compare_with_value;
      if (rule.compare_with_value) {
        match.right_result_text = trim(rule.right_value_text);
        match.right_value = threshold;
      } else {
        match.right_entry_id = rightIt->second.entry_id;
        match.right_item_code = rule.right_item_code;
        match.right_item_name = rule.right_item_name.empty()
                                    ? rightIt->second.item_name
                                    : rule.right_item_name;
        match.right_result_text = trim(rightIt->second.result);
        match.right_value = rightValue;
      }
      match.fingerprint = fingerprint(match);
      matches.push_back(std::move(match));
    }
  }
  return matches;
}

} // namespace scheduled_check
