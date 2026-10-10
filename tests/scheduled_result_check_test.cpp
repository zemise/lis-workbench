#include "scheduled_result_check_core.h"

#include <iostream>
#include <limits>

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

  // A op (B * multiplier): boundaries, raw result snapshots, and all operators.
  auto scaledRows = std::vector<scheduled_check::ResultRow>{rows[0], rows[1]};
  scaledRows[0].result = "20";
  scaledRows[1].result = "5";
  rule.right_multiplier_text = "4";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  rule.op = ">=";
  matches = scheduled_check::evaluate({rule}, scaledRows);
  CHECK(matches.size() == 1 && matches[0].right_multiplier_text == "4" &&
        matches[0].right_result_text == "5" && matches[0].right_value == 5.0);
  rule.op = "=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  rule.op = "<=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  rule.op = "!=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  scaledRows[0].result = "20.1";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  rule.op = ">";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  scaledRows[0].result = "19.9";
  rule.op = "<";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  rule.right_multiplier_text = "0.5";
  scaledRows[0].result = "2.5";
  rule.op = "=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  CHECK(!scheduled_check::needs_result_followup({rule}, scaledRows));
  CHECK(scheduled_check::needs_result_followup({rule}, {scaledRows[0]}));
  scaledRows[1].rep_no = "R2";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  scaledRows[1].rep_no = "R1";
  scaledRows[1].result = "<5";
  CHECK(scheduled_check::evaluate({rule}, scaledRows, &skipped).empty() && skipped == 1);
  scaledRows[1].result = "5";
  for (const char *invalid : {"", "0", "-4", "nan", "inf", "4倍", "1e999"}) {
    rule.right_multiplier_text = invalid;
    CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  }
  rule.right_multiplier_text = "1e308"; // Overflow must never produce a match.
  rule.op = "!=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  rule.right_multiplier_text = "4";
  scaledRows[0].result = "0";
  scaledRows[1].result = "0";
  rule.op = "=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  scaledRows[0].result = "-8";
  scaledRows[1].result = "-2";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);

  // Relative error uses the target, not the left result; edges are inclusive.
  rule.op = "!=";
  rule.right_multiplier_text = "3";
  rule.tolerance_percent_text = "3";
  scaledRows[1].result = "5"; // target 15, inclusive range 14.55..15.45
  scaledRows[0].result = "15";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  scaledRows[0].result = "14.55";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  scaledRows[0].result = "15.45";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  scaledRows[0].result = "15.451";
  matches = scheduled_check::evaluate({rule}, scaledRows);
  CHECK(matches.size() == 1 && matches[0].tolerance_percent_text == "3" &&
        matches[0].right_result_text == "5" && matches[0].right_value == 5.0);
  scaledRows[0].result = "14.549";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  for (const char *invalid : {"", "-3", "100.1", "nan", "inf", "3%"}) {
    rule.tolerance_percent_text = invalid;
    CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  }
  rule.tolerance_percent_text = "0";
  scaledRows[0].result = "15.1";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);
  rule.tolerance_percent_text = "3";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).empty());
  rule.op = "=";
  CHECK(scheduled_check::evaluate({rule}, scaledRows).size() == 1);

  using scheduled_check::compare_with_tolerance;
  CHECK(compare_with_tolerance(103.0, "=", 100.0, 3.0));
  CHECK(compare_with_tolerance(97.0, "=", 100.0, 3.0));
  CHECK(!compare_with_tolerance(103.0, ">", 100.0, 3.0));
  CHECK(compare_with_tolerance(103.001, ">", 100.0, 3.0));
  CHECK(!compare_with_tolerance(97.0, "<", 100.0, 3.0));
  CHECK(compare_with_tolerance(96.999, "<", 100.0, 3.0));
  CHECK(compare_with_tolerance(97.0, ">=", 100.0, 3.0));
  CHECK(!compare_with_tolerance(96.999, ">=", 100.0, 3.0));
  CHECK(compare_with_tolerance(103.0, "<=", 100.0, 3.0));
  CHECK(!compare_with_tolerance(103.001, "<=", 100.0, 3.0));
  CHECK(compare_with_tolerance(-103.0, "=", -100.0, 3.0));
  CHECK(compare_with_tolerance(-97.0, "=", -100.0, 3.0));
  CHECK(compare_with_tolerance(-96.999, ">", -100.0, 3.0));
  CHECK(compare_with_tolerance(0.0, "=", 0.0, 3.0));
  CHECK(compare_with_tolerance(0.001, "!=", 0.0, 3.0));
  CHECK(compare_with_tolerance(100.5, "=", 100.0, 0.5));
  CHECK(!compare_with_tolerance(100.501, "=", 100.0, 0.5));
  CHECK(compare_with_tolerance(200.0, "=", 100.0, 100.0));
  CHECK(!compare_with_tolerance(1.0, "unknown", 1.0, 3.0));
  CHECK(!compare_with_tolerance(1.0, "!=", 1.0, -1.0));
  CHECK(!compare_with_tolerance(1.0, "!=", 1.0, 101.0));
  CHECK(!compare_with_tolerance(1.0, "!=", std::numeric_limits<double>::infinity(), 3.0));
  CHECK(!compare_with_tolerance(1.0, "!=", 1.0, std::numeric_limits<double>::quiet_NaN()));
  CHECK(!compare_with_tolerance(1.0, "!=", std::numeric_limits<double>::max(), 100.0));
  for (const char *op : {">", ">=", "<", "<=", "=", "!="})
    CHECK(compare_with_tolerance(2.0, op, 1.0, 0.0) ==
          scheduled_check::compare_numbers(2.0, op, 1.0));

  scheduled_check::Rule thresholdRule;
  thresholdRule.id = 8;
  thresholdRule.name = "A大于5";
  thresholdRule.left_item_code = "A";
  thresholdRule.left_item_name = "项目A";
  thresholdRule.op = ">";
  thresholdRule.compare_with_value = true;
  thresholdRule.right_value_text = "5.0";
  thresholdRule.tolerance_percent_text = "invalid"; // Literal mode ignores error band.
  thresholdRule.right_multiplier_text = "invalid"; // Literal mode ignores multiplier.
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
  // Compound rules: AND/OR, NOT, unknown values, grouping and snapshots.
  scheduled_check::Rule group;
  group.id = 10;
  group.name = "比例及附加条件";
  group.left_item_code = "Hb";
  group.right_item_code = "RBC";
  group.op = "!=";
  group.right_multiplier_text = "3";
  group.tolerance_percent_text = "3";
  scheduled_check::Condition extra;
  extra.left_item_code = "MCV";
  extra.op = ">=";
  extra.compare_with_value = true;
  extra.right_value_text = "80";
  group.extra_conditions.push_back(extra);
  extra.op = "<=";
  extra.right_value_text = "100";
  group.extra_conditions.push_back(extra);
  std::vector<scheduled_check::ResultRow> groupRows(3);
  const char *codes[] = {"Hb", "RBC", "MCV"};
  const char *values[] = {"16", "5", "90"};
  for (int i = 0; i < 3; ++i) {
    groupRows[i].rep_no = "R3";
    groupRows[i].item_code = codes[i];
    groupRows[i].result = values[i];
    groupRows[i].entry_id = std::to_string(i + 1);
  }
  matches = scheduled_check::evaluate({group}, groupRows);
  CHECK(matches.size() == 1);
  CHECK(matches[0].condition_summary.find("；并 ") != std::string::npos);
  CHECK(matches[0].condition_item_codes == "Hb\nMCV\nRBC");
  CHECK(matches[0].left_item_code == "Hb" && matches[0].right_result_text == "5");
  const std::string firstGroupFingerprint = matches[0].fingerprint;
  groupRows[2].result = "91";
  matches = scheduled_check::evaluate({group}, groupRows);
  CHECK(matches.size() == 1 && matches[0].fingerprint != firstGroupFingerprint);
  groupRows[2].result = "101";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  groupRows[2].result = "100";
  CHECK(scheduled_check::evaluate({group}, groupRows).size() == 1);
  groupRows[2].result = "79";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  groupRows[2].result = "80";
  CHECK(scheduled_check::evaluate({group}, groupRows).size() == 1);
  groupRows[2].rep_no = "R4";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  CHECK(scheduled_check::needs_result_followup({group}, groupRows));
  groupRows[2].rep_no = "R3";
  groupRows[2].result = "阴性";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  group.extra_conditions[0].negate = true;
  // NOT unknown remains unknown; it never becomes true.
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  CHECK(scheduled_check::evaluate({group}, {groupRows[0], groupRows[1]}).empty());
  CHECK(scheduled_check::needs_result_followup({group}, {groupRows[0], groupRows[1]}));
  group.extra_conditions[0].negate = false;
  group.match_any = true;
  matches = scheduled_check::evaluate({group}, groupRows);
  CHECK(matches.size() == 1 && matches[0].condition_summary.find("无法判断") != std::string::npos);
  CHECK(matches[0].condition_summary.find("；或 ") != std::string::npos);
  CHECK(scheduled_check::evaluate({group}, {groupRows[0], groupRows[1]}).size() == 1);
  groupRows[0].result = "15"; // First condition false; later condition decides OR.
  groupRows[2].result = "90";
  matches = scheduled_check::evaluate({group}, groupRows);
  CHECK(matches.size() == 1 && matches[0].left_item_code == "MCV");
  group.extra_conditions.resize(1);
  groupRows[2].result = "79";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  group.extra_conditions[0].negate = true;
  CHECK(scheduled_check::evaluate({group}, groupRows).size() == 1);
  groupRows[2].result = "";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  group.negate = true;
  CHECK(scheduled_check::evaluate({group}, groupRows).size() == 1);
  group.room_code = "102";
  group.mach_code = "1";
  for (auto &r : groupRows) { r.room_code = "102"; r.mach_code = "1"; }
  CHECK(scheduled_check::evaluate({group}, groupRows).size() == 1);
  groupRows[0].mach_code = "2";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  groupRows[0].mach_code = "1";
  group.extra_conditions[0].op = "invalid";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty()); // Invalid branches cannot be hidden by OR.
  group.extra_conditions[0].op = ">=";
  group.enabled = false;
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());
  group.enabled = true;
  group.left_item_name = "Hb:血红蛋白";
  group.extra_conditions[0].left_item_name = "MCV:|红细胞平均体积";
  const auto encoded = scheduled_check::serialize_condition_group(group);
  scheduled_check::Rule decoded = group;
  decoded.extra_conditions.clear(); decoded.negate = false; decoded.match_any = false;
  CHECK(scheduled_check::deserialize_condition_group(encoded, decoded));
  CHECK(scheduled_check::serialize_condition_group(decoded) == encoded);
  CHECK(decoded.match_any && decoded.negate && decoded.extra_conditions[0].negate);
  CHECK(decoded.extra_conditions[0].left_item_name == group.extra_conditions[0].left_item_name);
  CHECK(scheduled_check::rule_item_codes(decoded) == scheduled_check::rule_item_codes(group));
  CHECK(!scheduled_check::deserialize_condition_group(encoded + "x", decoded));
  CHECK(!scheduled_check::deserialize_condition_group(encoded.substr(0, encoded.size() - 1), decoded));
  CHECK(!scheduled_check::deserialize_condition_group("3:0:0:0:", decoded));
  CHECK(!scheduled_check::deserialize_condition_group("1:2:0:0:", decoded));
  CHECK(!scheduled_check::deserialize_condition_group("1:0:0:64:", decoded));
  CHECK(!scheduled_check::deserialize_condition_group("1:0:0:1:99999999999999999:x", decoded));
  CHECK(scheduled_check::deserialize_condition_group("", decoded));
  CHECK(!decoded.match_any && !decoded.negate && decoded.extra_conditions.empty());
  CHECK(scheduled_check::serialize_condition_group(decoded).empty());
  // Computation overflow is unknown even when the condition is negated.
  group.extra_conditions.clear(); group.match_any = false;
  group.negate = true; group.right_multiplier_text = "1e308";
  groupRows[0].result = "1"; groupRows[1].result = "2";
  CHECK(scheduled_check::evaluate({group}, groupRows).empty());

  // Mixed connectors: AND / AND NOT bind more tightly than OR.
  scheduled_check::Rule mixed;
  mixed.id = 9;
  mixed.left_item_code = "A"; mixed.op = ">";
  mixed.compare_with_value = true; mixed.right_value_text = "0";
  scheduled_check::Condition b = mixed, c = mixed;
  b.left_item_code = "B"; b.join = scheduled_check::ConditionJoin::any;
  c.left_item_code = "C"; c.join = scheduled_check::ConditionJoin::all;
  mixed.extra_conditions = {b, c};
  std::vector<scheduled_check::ResultRow> mixedRows(3);
  for (int i = 0; i < 3; ++i) {
    mixedRows[i].rep_no = "M1"; mixedRows[i].item_code = std::string(1, 'A' + i);
    mixedRows[i].entry_id = std::to_string(i + 1); mixedRows[i].result = "1";
  }
  mixedRows[0].result = "0"; mixedRows[2].result = "0";
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).empty()); // A OR (B AND C).
  mixedRows[0].result = "1";
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).size() == 1); // A wins independently.
  mixedRows[0].result = "0";
  mixed.extra_conditions[1].negate = true;
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).size() == 1); // A OR (B AND NOT C).
  mixedRows[2].result = "阴性";
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).empty()); // NOT unknown remains unknown.
  mixedRows[0].result = "1";
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).size() == 1);
  mixed.extra_conditions[0].join = scheduled_check::ConditionJoin::all;
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).empty());
  mixed.extra_conditions[1].join = scheduled_check::ConditionJoin::any;
  mixed.extra_conditions[1].negate = false;
  mixedRows[0].result = "0"; mixedRows[2].result = "1";
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).size() == 1); // (A AND B) OR C.
  for (int mask = 0; mask < 8; ++mask) {
    const bool av = (mask & 1) != 0, bv = (mask & 2) != 0, cv = (mask & 4) != 0;
    mixedRows[0].result = av ? "1" : "0";
    mixedRows[1].result = bv ? "1" : "0";
    mixedRows[2].result = cv ? "1" : "0";
    mixed.extra_conditions[0].join = scheduled_check::ConditionJoin::any;
    mixed.extra_conditions[1].join = scheduled_check::ConditionJoin::all;
    mixed.extra_conditions[1].negate = false;
    CHECK(!scheduled_check::evaluate({mixed}, mixedRows).empty() == (av || (bv && cv)));
    mixed.extra_conditions[1].negate = true;
    CHECK(!scheduled_check::evaluate({mixed}, mixedRows).empty() == (av || (bv && !cv)));
    mixed.extra_conditions[0].join = scheduled_check::ConditionJoin::all;
    mixed.extra_conditions[1].join = scheduled_check::ConditionJoin::any;
    mixed.extra_conditions[1].negate = false;
    const auto combinedMatches = scheduled_check::evaluate({mixed}, mixedRows);
    CHECK(!combinedMatches.empty() == ((av && bv) || cv));
    if (av && !bv && cv) CHECK(combinedMatches[0].left_item_code == "C");
  }
  const auto mixedEncoded = scheduled_check::serialize_condition_group(mixed);
  scheduled_check::Rule mixedDecoded = mixed;
  CHECK(scheduled_check::deserialize_condition_group(mixedEncoded, mixedDecoded));
  CHECK(scheduled_check::serialize_condition_group(mixedDecoded) == mixedEncoded);
  CHECK(mixedDecoded.extra_conditions[0].join == scheduled_check::ConditionJoin::all);
  CHECK(mixedDecoded.extra_conditions[1].join == scheduled_check::ConditionJoin::any);
  CHECK(scheduled_check::rule_description(mixedDecoded).find(" 或 ") != std::string::npos);
  auto invalidJoin = mixedEncoded;
  invalidJoin.back() = '9';
  CHECK(!scheduled_check::deserialize_condition_group(invalidJoin, mixedDecoded));
  // Version 1 omitted per-row connectors: retain the old group mode and NOT.
  auto legacyEncoded = encoded;
  legacyEncoded[0] = '1';
  legacyEncoded.resize(legacyEncoded.size() - 3); // Last field: 1:0 (legacy connector).
  CHECK(scheduled_check::deserialize_condition_group(legacyEncoded, decoded));
  CHECK(decoded.match_any && decoded.negate && decoded.extra_conditions[0].negate);
  CHECK(decoded.extra_conditions[0].join == scheduled_check::ConditionJoin::legacy);
  CHECK(scheduled_check::joins_with_or(decoded, decoded.extra_conditions[0]));
  mixed.extra_conditions[0].join = static_cast<scheduled_check::ConditionJoin>(9);
  std::string joinError;
  CHECK(!scheduled_check::validate_condition(mixed.extra_conditions[0], joinError));
  CHECK(scheduled_check::evaluate({mixed}, mixedRows).empty());

  std::cout << "scheduled result check tests passed\n";
  return 0;
}
