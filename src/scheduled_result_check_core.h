#pragma once

#include <string>
#include <vector>

namespace scheduled_check {

struct Rule {
  int id = 0;
  bool enabled = true;
  std::string name;
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
  std::string left_result_text;
  double left_value = 0.0;
  std::string op;
  std::string right_entry_id;
  std::string right_item_code;
  std::string right_item_name;
  std::string right_result_text;
  double right_value = 0.0;
  // Mirrors Rule::compare_with_value so UI/notification rendering can show a
  // threshold as "项目A 结果 > 5.0" instead of "项目A 结果 >  5.0".
  bool compare_with_value = false;
  std::string fingerprint;
};

bool parse_number(const std::string &text, double &value);
bool compare_numbers(double left, const std::string &op, double right);
std::vector<Match> evaluate(const std::vector<Rule> &rules,
                            const std::vector<ResultRow> &rows,
                            int *skipped_non_numeric = nullptr);

} // namespace scheduled_check
