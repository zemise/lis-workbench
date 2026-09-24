#include "scheduled_result_check_core.h"

#include <iostream>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "check failed line " << __LINE__ << ": " #x "\n";           \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  double value = 0.0;
  CHECK(scheduled_check::parse_number(" -8.81 ", value) && value == -8.81);
  CHECK(scheduled_check::parse_number("+3.5", value) && value == 3.5);
  CHECK(!scheduled_check::parse_number("< 10.00", value));
  CHECK(!scheduled_check::parse_number("1+", value));
  CHECK(!scheduled_check::parse_number("", value));
  CHECK(!scheduled_check::parse_number("nan", value));
  CHECK(!scheduled_check::parse_number("inf", value));
  CHECK(scheduled_check::compare_numbers(2.0, ">", 1.0));
  CHECK(scheduled_check::compare_numbers(2.0, ">=", 2.0));
  CHECK(scheduled_check::compare_numbers(1.0, "<", 2.0));
  CHECK(scheduled_check::compare_numbers(2.0, "<=", 2.0));
  CHECK(scheduled_check::compare_numbers(1.0, "=", 1.0 + 1e-12));
  CHECK(scheduled_check::compare_numbers(1.0, "!=", 1.1));
  CHECK(!scheduled_check::compare_numbers(1.0, "unknown", 1.0));
  CHECK(scheduled_check::item_display_name(" CK ", "肌酸激酶", "201924") == "CK");
  CHECK(scheduled_check::item_display_name(" ", "肌酸激酶", "201924") == "肌酸激酶");
  CHECK(scheduled_check::item_display_name("", "", "201924") == "201924");

  scheduled_check::Rule rule;
  rule.id = 7;
  rule.name = "A大于B";
  rule.left_item_code = "A";
  rule.left_item_name = "项目A";
  rule.op = ">";
  rule.right_item_code = "B";
  rule.right_item_name = "项目B";
  std::vector<scheduled_check::ResultRow> rows(3);
  rows[0].entry_id = "1";
  rows[0].rep_no = "R1";
  rows[0].item_code = "A";
  rows[0].item_eng = "ALT";
  rows[0].result = "2.5";
  rows[1].entry_id = "2";
  rows[1].rep_no = "R1";
  rows[1].item_code = "B";
  rows[1].item_eng = "AST";
  rows[1].result = "1.5";
  rows[2].entry_id = "3";
  rows[2].rep_no = "R2";
  rows[2].item_code = "A";
  rows[2].result = "9";
  int skipped = 0;
  auto matches = scheduled_check::evaluate({rule}, rows, &skipped);
  CHECK(matches.size() == 1 && matches[0].rep_no == "R1" && skipped == 0);
  rule.room_code = "10";
  rule.mach_code = "2015";
  CHECK(scheduled_check::evaluate({rule}, rows).empty());
  rows[0].room_code = rows[1].room_code = "10";
  rows[0].mach_code = rows[1].mach_code = "2015";
  CHECK(scheduled_check::evaluate({rule}, rows).size() == 1);
  rows[0].mach_code = "2016";
  CHECK(scheduled_check::evaluate({rule}, rows).empty());
  rows[0].mach_code = "2015";
  rows[1].mach_code = "2016";
  CHECK(scheduled_check::evaluate({rule}, rows).empty());
  rows[1].mach_code = "2015";
  rule.room_code.clear();
  rule.mach_code.clear();
  CHECK(!scheduled_check::needs_result_followup({rule}, {rows[0], rows[1]}));
  CHECK(scheduled_check::needs_result_followup({rule}, {rows[0]}));
  CHECK(!scheduled_check::needs_result_followup({rule}, {}));
  auto unrelated = rows[2];
  unrelated.item_code = "C";
  CHECK(!scheduled_check::needs_result_followup({rule}, {unrelated}));
  CHECK(matches[0].left_item_eng == "ALT" &&
        matches[0].right_item_eng == "AST");
  scheduled_check::ResultRow olderNonEmpty = rows[0];
  olderNonEmpty.entry_id = "9";
  olderNonEmpty.result = "3.5";
  scheduled_check::ResultRow newerEmpty = rows[0];
  newerEmpty.entry_id = "100";
  newerEmpty.result = "";
  auto duplicateRows = std::vector<scheduled_check::ResultRow>{
      olderNonEmpty, newerEmpty, rows[1]};
  matches = scheduled_check::evaluate({rule}, duplicateRows, &skipped);
  CHECK(matches.size() == 1 && matches[0].left_result_text == "3.5");

  auto splitReportRows =
      std::vector<scheduled_check::ResultRow>{rows[0], rows[1]};
  splitReportRows[1].rep_no = "R2";
  matches = scheduled_check::evaluate({rule}, splitReportRows, &skipped);
  CHECK(matches.empty());

  rule.enabled = false;
  matches = scheduled_check::evaluate({rule}, rows, &skipped);
  CHECK(matches.empty());
  rule.enabled = true;
  rows[1].result = "阴性";
  CHECK(scheduled_check::needs_result_followup({rule}, {rows[0], rows[1]}));
  matches = scheduled_check::evaluate({rule}, rows, &skipped);
  CHECK(matches.empty() && skipped == 1);

  scheduled_check::Rule thresholdRule;
  thresholdRule.id = 8;
  thresholdRule.name = "A大于5";
  thresholdRule.left_item_code = "A";
  thresholdRule.left_item_name = "项目A";
  thresholdRule.op = ">";
  thresholdRule.compare_with_value = true;
  thresholdRule.right_value_text = "5.0";
  std::vector<scheduled_check::ResultRow> valueRows(2);
  valueRows[0].entry_id = "1";
  valueRows[0].rep_no = "R1";
  valueRows[0].item_code = "A";
  valueRows[0].result = "2.5";
  valueRows[1].entry_id = "2";
  valueRows[1].rep_no = "R2";
  valueRows[1].item_code = "A";
  valueRows[1].result = "9";
  matches = scheduled_check::evaluate({thresholdRule}, valueRows, &skipped);
  CHECK(matches.size() == 1 && matches[0].rep_no == "R2" &&
        matches[0].compare_with_value &&
        matches[0].right_result_text == "5.0");
  CHECK(!scheduled_check::needs_result_followup({thresholdRule}, valueRows));
  thresholdRule.right_value_text = "10";
  matches = scheduled_check::evaluate({thresholdRule}, valueRows, &skipped);
  CHECK(matches.empty());
  valueRows[1].result = "阴性";
  matches = scheduled_check::evaluate({thresholdRule}, valueRows, &skipped);
  CHECK(matches.empty() && skipped == 1);
  std::cout << "scheduled result check tests passed\n";
  return 0;
}
