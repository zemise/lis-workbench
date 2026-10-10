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

bool compare_with_tolerance(double left, const std::string &op, double target,
                            double percent) {
  if (!std::isfinite(left) || !std::isfinite(target) ||
      !std::isfinite(percent) || percent < 0.0 || percent > 100.0)
    return false;
  if (percent == 0.0)
    return compare_numbers(left, op, target);
  const double delta = std::fabs(target) * (percent / 100.0);
  const double lower = target - delta, upper = target + delta;
  if (!std::isfinite(lower) || !std::isfinite(upper))
    return false;
  const bool below = compare_numbers(left, "<", lower);
  const bool above = compare_numbers(left, ">", upper);
  if (op == "=")
    return !below && !above;
  if (op == "!=")
    return below || above;
  if (op == ">")
    return above;
  if (op == "<")
    return below;
  if (op == ">=")
    return !below;
  if (op == "<=")
    return !above;
  return false;
}

std::string item_display_name(const std::string &english,
                              const std::string &chinese,
                              const std::string &code) {
  const auto eng = trim(english);
  if (!eng.empty())
    return eng;
  const auto name = trim(chinese);
  return name.empty() ? trim(code) : name;
}

bool validate_condition(const Condition &c, std::string &error) {
  double value = 0.0;
  if (c.left_item_code.empty() ||
      (c.op != ">" && c.op != ">=" && c.op != "<" && c.op != "<=" &&
       c.op != "=" && c.op != "!=")) {
    error = "请选择项目和比较关系";
  } else if (c.compare_with_value) {
    if (parse_number(c.right_value_text, value)) {
      error.clear();
      return true;
    }
    error = "固定值必须是有效数字";
  } else if (c.right_item_code.empty() || c.left_item_code == c.right_item_code) {
    error = "请选择两个不同的项目";
  } else if (!parse_number(c.right_multiplier_text, value) || value <= 0.0) {
    error = "项目 B 倍数必须是大于 0 的有效数字";
  } else if (!parse_number(c.tolerance_percent_text, value) ||
             value < 0.0 || value > 100.0) {
    error = "允许误差必须是 0 至 100 的有效百分数";
  } else {
    error.clear();
    return true;
  }
  return false;
}

std::string condition_description(const Condition &c) {
  const auto name = [](const std::string &label, const std::string &code) {
    return label.empty() ? code : label;
  };
  std::string result = name(c.left_item_name, c.left_item_code) + " " + c.op + " ";
  if (c.compare_with_value) {
    result += c.right_value_text;
  } else {
    result += name(c.right_item_name, c.right_item_code);
    double value = 0.0;
    if (!parse_number(c.right_multiplier_text, value) || value != 1.0)
      result += " × " + c.right_multiplier_text;
    if (parse_number(c.tolerance_percent_text, value) && value != 0.0)
      result += "（±" + c.tolerance_percent_text + "%）";
  }
  return c.negate ? "非（" + result + "）" : result;
}

std::vector<Condition> rule_conditions(const Rule &rule) {
  std::vector<Condition> result{static_cast<const Condition &>(rule)};
  result.insert(result.end(), rule.extra_conditions.begin(), rule.extra_conditions.end());
  return result;
}

std::vector<std::string> rule_item_codes(const Rule &rule) {
  std::vector<std::string> result;
  for (const auto &c : rule_conditions(rule)) {
    if (!c.left_item_code.empty()) result.push_back(c.left_item_code);
    if (!c.compare_with_value && !c.right_item_code.empty())
      result.push_back(c.right_item_code);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string serialize_condition_group(const Rule &rule) {
  if (!rule.negate && !rule.match_any && rule.extra_conditions.empty()) return {};
  std::ostringstream out;
  out << "1:" << rule.match_any << ':' << rule.negate << ':' << rule.extra_conditions.size() << ':';
  const auto field = [&out](const std::string &value) {
    out << value.size() << ':' << value;
  };
  for (const auto &c : rule.extra_conditions) {
    field(c.left_item_code); field(c.left_item_name); field(c.left_item_unit);
    field(c.op); field(c.right_item_code); field(c.right_item_name); field(c.right_item_unit);
    field(c.compare_with_value ? "1" : "0"); field(c.right_value_text);
    field(c.right_multiplier_text); field(c.tolerance_percent_text); field(c.negate ? "1" : "0");
  }
  return out.str();
}

bool deserialize_condition_group(const std::string &data, Rule &rule) {
  Rule decoded = rule;
  decoded.match_any = false; decoded.negate = false; decoded.extra_conditions.clear();
  if (data.empty()) { rule = std::move(decoded); return true; }
  size_t pos = 0;
  const auto number = [&data, &pos](size_t &n) {
    n = 0;
    const size_t start = pos;
    while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9') {
      if (n > 100000) return false;
      n = n * 10 + static_cast<size_t>(data[pos++] - '0');
    }
    if (pos == start || pos >= data.size() || data[pos++] != ':' || n > 1000000) return false;
    return true;
  };
  const auto field = [&data, &pos, &number](std::string &value) {
    size_t n = 0;
    if (!number(n) || n > data.size() - pos) return false;
    value = data.substr(pos, n); pos += n; return true;
  };
  size_t version = 0, any = 0, negate = 0, count = 0;
  if (!number(version) || version != 1 || !number(any) || any > 1 ||
      !number(negate) || negate > 1 || !number(count) || count > 63) return false;
  decoded.match_any = any != 0; decoded.negate = negate != 0;
  for (size_t i = 0; i < count; ++i) {
    Condition c;
    std::string literal, inverted;
    if (!field(c.left_item_code) || !field(c.left_item_name) || !field(c.left_item_unit) ||
        !field(c.op) || !field(c.right_item_code) || !field(c.right_item_name) ||
        !field(c.right_item_unit) || !field(literal) || !field(c.right_value_text) ||
        !field(c.right_multiplier_text) || !field(c.tolerance_percent_text) || !field(inverted) ||
        (literal != "0" && literal != "1") || (inverted != "0" && inverted != "1")) return false;
    c.compare_with_value = literal == "1"; c.negate = inverted == "1";
    decoded.extra_conditions.push_back(std::move(c));
  }
  if (pos != data.size()) return false;
  rule = std::move(decoded);
  return true;
}

namespace {
using Results = std::map<std::string, ResultRow>;
enum class Truth { unknown, no, yes };

void select_result(Results &results, const ResultRow &row) {
  auto &selected = results[row.item_code];
  const bool selectedEmpty = trim(selected.result).empty();
  const bool candidateEmpty = trim(row.result).empty();
  if (selected.entry_id.empty() || (selectedEmpty && !candidateEmpty) ||
      (selectedEmpty == candidateEmpty && newer_entry_id(row.entry_id, selected.entry_id)))
    selected = row;
}

Truth evaluate_condition(const Condition &c, const Rule &rule, const Results &results,
                         Match &match, int *skipped) {
  const auto left = results.find(c.left_item_code);
  const auto right = c.compare_with_value ? results.end() : results.find(c.right_item_code);
  const auto in_scope = [&rule](const ResultRow &row) {
    return rule.mach_code.empty() || (row.mach_code == rule.mach_code && row.room_code == rule.room_code);
  };
  if (left == results.end() || !in_scope(left->second) ||
      (!c.compare_with_value && (right == results.end() || !in_scope(right->second)))) return Truth::unknown;
  double leftValue = 0.0, rightValue = 0.0, multiplier = 1.0, percent = 0.0;
  if (!parse_number(left->second.result, leftValue) ||
      !parse_number(c.compare_with_value ? c.right_value_text : right->second.result, rightValue)) {
    if (skipped) ++*skipped;
    return Truth::unknown;
  }
  if (!c.compare_with_value) {
    parse_number(c.right_multiplier_text, multiplier);
    parse_number(c.tolerance_percent_text, percent);
  }
  const double target = rightValue * multiplier;
  const double delta = std::fabs(target) * (percent / 100.0);
  if (!std::isfinite(target) || !std::isfinite(target - delta) || !std::isfinite(target + delta))
    return Truth::unknown;
  match.rule_id = rule.id; match.rule_name = rule.name;
  match.rep_no = left->second.rep_no; match.oper_no = left->second.oper_no;
  match.room_code = left->second.room_code; match.mach_code = left->second.mach_code;
  match.mach_name = left->second.mach_name; match.inspect_date = left->second.inspect_date;
  match.left_entry_id = left->second.entry_id; match.left_item_code = c.left_item_code;
  match.left_item_name = c.left_item_name.empty() ? left->second.item_name : c.left_item_name;
  match.left_item_eng = left->second.item_eng;
  match.left_result_text = trim(left->second.result); match.left_value = leftValue;
  match.op = c.op; match.compare_with_value = c.compare_with_value;
  match.right_result_text = trim(c.compare_with_value ? c.right_value_text : right->second.result);
  match.right_value = rightValue;
  if (!c.compare_with_value) {
    match.right_multiplier_text = trim(c.right_multiplier_text);
    match.tolerance_percent_text = trim(c.tolerance_percent_text);
    match.right_entry_id = right->second.entry_id; match.right_item_code = c.right_item_code;
    match.right_item_name = c.right_item_name.empty() ? right->second.item_name : c.right_item_name;
    match.right_item_eng = right->second.item_eng;
  }
  const bool satisfied = compare_with_tolerance(leftValue, c.op, target, percent);
  return (c.negate ? !satisfied : satisfied) ? Truth::yes : Truth::no;
}
} // namespace

std::vector<Match> evaluate(const std::vector<Rule> &rules,
                            const std::vector<ResultRow> &rows,
                            int *skipped_non_numeric) {
  if (skipped_non_numeric) *skipped_non_numeric = 0;
  std::map<std::string, Results> reports;
  for (const auto &row : rows) select_result(reports[row.rep_no], row);
  std::vector<Match> matches;
  for (const auto &rule : rules) {
    if (!rule.enabled) continue;
    const auto conditions = rule_conditions(rule);
    std::string error;
    if (conditions.size() > 64 || std::any_of(conditions.begin(), conditions.end(),
        [&error](const Condition &c) { return !validate_condition(c, error); })) continue;
    for (const auto &report : reports) {
      bool any = false, all = true;
      Match representative;
      std::ostringstream summary, groupFingerprint;
      summary << (rule.match_any ? "满足任一：" : "满足全部：");
      for (size_t i = 0; i < conditions.size(); ++i) {
        Match m;
        const auto state = evaluate_condition(conditions[i], rule, report.second, m, skipped_non_numeric);
        if (state == Truth::yes && !any) representative = m;
        any = any || state == Truth::yes;
        all = all && state == Truth::yes;
        if (i) summary << "；";
        summary << condition_description(conditions[i]) << " ["
                << (state == Truth::unknown ? "无法判断" : state == Truth::yes ? "满足" : "不满足");
        const auto raw = [&report](const std::string &code) {
          const auto it = report.second.find(code);
          return it == report.second.end() || trim(it->second.result).empty()
                     ? std::string("缺失") : trim(it->second.result);
        };
        summary << "，" << raw(conditions[i].left_item_code);
        if (!conditions[i].compare_with_value) summary << " / " << raw(conditions[i].right_item_code);
        summary << ']';
        // Include all participating raw results (also unresolved OR branches).
        groupFingerprint << static_cast<int>(state) << ':';
        for (const auto &code : {conditions[i].left_item_code,
                                conditions[i].compare_with_value ? std::string{} : conditions[i].right_item_code}) {
          const auto it = report.second.find(code);
          const std::string value = it == report.second.end() ? "" : it->second.entry_id + "|" + it->second.result;
          groupFingerprint << value.size() << ':' << value;
        }
      }
      if (!(rule.match_any ? any : all)) continue;
      representative.fingerprint = fingerprint(representative);
      if (conditions.size() > 1 || rule.negate || rule.match_any) {
        representative.condition_summary = summary.str();
        for (const auto &code : rule_item_codes(rule)) {
          if (!representative.condition_item_codes.empty()) representative.condition_item_codes += '\n';
          representative.condition_item_codes += code;
        }
        representative.fingerprint += "|group:" + groupFingerprint.str();
      }
      matches.push_back(std::move(representative));
    }
  }
  return matches;
}

bool needs_result_followup(const std::vector<Rule> &rules,
                           const std::vector<ResultRow> &rows) {
  // Group by report so values from separate reports never complete a condition.
  std::map<std::string, Results> reports;
  for (const auto &row : rows) select_result(reports[row.rep_no], row);
  for (const auto &rule : rules) {
    if (!rule.enabled) continue;
    const auto codes = rule_item_codes(rule);
    for (const auto &report : reports) {
      if (std::none_of(codes.begin(), codes.end(), [&](const std::string &code) {
            const auto it = report.second.find(code);
            return it != report.second.end() && (rule.mach_code.empty() ||
                (it->second.mach_code == rule.mach_code && it->second.room_code == rule.room_code));
          })) continue;
      for (const auto &code : codes) {
        const auto it = report.second.find(code);
        double value = 0.0;
        if (it == report.second.end() || !parse_number(it->second.result, value) ||
            (!rule.mach_code.empty() && (it->second.mach_code != rule.mach_code ||
                                        it->second.room_code != rule.room_code))) return true;
      }
    }
  }
  return false;
}

} // namespace scheduled_check
