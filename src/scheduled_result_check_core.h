#pragma once

#include <string>
#include <vector>

namespace scheduled_check {

struct Condition {
  std::string left_item_code;
  std::string left_item_name;
  std::string left_item_unit;
  std::string op;
  std::string right_item_code;
  std::string right_item_name;
  std::string right_item_unit;
  // When true the right side is a literal threshold (right_value_text) and the
  // right_item_* fields are ignored.
  bool compare_with_value = false;
  std::string right_value_text;
  // Compare A with B * multiplier; ignored for literal thresholds.
  std::string right_multiplier_text = "1";
  // Relative percent of |B * multiplier|; literal mode ignores this field.
  std::string tolerance_percent_text = "0";
  bool negate = false;
};

// The inherited condition is the first row, preserving legacy rule fields.
struct Rule : Condition {
  int id = 0;
  bool enabled = true;
  std::string name;
  // Empty machine code denotes a legacy, unscoped rule.
  std::string room_code;
  std::string mach_code;
  std::string mach_name;
  bool match_any = false;
  std::vector<Condition> extra_conditions;
  std::string created_at;
  std::string updated_at;
};

struct ResultRow {
  std::string entry_id;
  std::string rep_no;
  std::string oper_no;
  std::string room_code;
  std::string mach_code;
  std::string mach_name;
  std::string inspect_date;
  std::string item_code;
  std::string item_name;
  std::string item_eng;
  std::string result;
};

struct Match {
  int rule_id = 0;
  std::string rule_name;
  std::string rep_no;
  std::string oper_no;
  std::string room_code;
  std::string mach_code;
  std::string mach_name;
  std::string inspect_date;
  std::string left_entry_id;
  std::string left_item_code;
  std::string left_item_name;
  std::string left_item_eng;
  std::string left_result_text;
  double left_value = 0.0;
  std::string op;
  std::string right_entry_id;
  std::string right_item_code;
  std::string right_item_name;
  std::string right_item_eng;
  std::string right_result_text;
  double right_value = 0.0;
  // Mirrors Rule::compare_with_value so UI/notification rendering can show a
  // threshold as "项目A 结果 > 5.0" instead of "项目A 结果 >  5.0".
  bool compare_with_value = false;
  // Preserve the rule multiplier and raw B result for historical rendering.
  std::string right_multiplier_text = "1";
  // Relative percent of |B * multiplier|; literal mode ignores this field.
  std::string tolerance_percent_text = "0";
  std::string fingerprint;
  // Complete group snapshot. Empty for an ordinary legacy single condition.
  std::string condition_summary;
  // Newline-separated item codes for highlighting all group items.
  std::string condition_item_codes;
};

bool parse_number(const std::string &text, double &value);
bool compare_numbers(double left, const std::string &op, double right);
// Inclusive band [target - |target| * percent / 100, target + ...].
// = inside, != outside, > above upper, < below lower, >= at/above lower,
// <= at/below upper. Zero percent preserves the original comparison.
bool compare_with_tolerance(double left, const std::string &op, double target,
                            double percent);
bool validate_condition(const Condition &condition, std::string &error);
std::string condition_description(const Condition &condition);
std::vector<Condition> rule_conditions(const Rule &rule);
std::vector<std::string> rule_item_codes(const Rule &rule);
// Versioned length-prefixed persistence; empty data means a legacy rule.
std::string serialize_condition_group(const Rule &rule);
bool deserialize_condition_group(const std::string &data, Rule &rule);
std::string item_display_name(const std::string &english,
                              const std::string &chinese,
                              const std::string &code);
std::vector<Match> evaluate(const std::vector<Rule> &rules,
                            const std::vector<ResultRow> &rows,
                            int *skipped_non_numeric = nullptr);
// A report is unfinished only when at least one selected rule item is present
// but a required numeric value is still absent/invalid.
bool needs_result_followup(const std::vector<Rule> &rules,
                           const std::vector<ResultRow> &rows);

} // namespace scheduled_check
