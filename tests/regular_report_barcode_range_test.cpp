#include "regular_report_barcode_range.h"

#include <iostream>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::cerr << "check failed line " << __LINE__ << ": " #x "\n"; \
            return 1;                                                          \
        }                                                                      \
    } while (false)

int main() {
    using regular_barcode::compare_sample_numbers;
    CHECK(compare_sample_numbers("001", "1") == 0);
    CHECK(compare_sample_numbers("9", "10") < 0);
    CHECK(compare_sample_numbers("99999999999999999999", "100000000000000000000") < 0);
    CHECK(compare_sample_numbers("A2", "A10") < 0);
    CHECK(compare_sample_numbers("a001", "A1") == 0);

    std::vector<search::ReportRow> rows(6);
    rows[0].id = "1";
    rows[0].oper_no = "010";
    rows[0].txm_no = "B10";
    rows[1].id = "2";
    rows[1].oper_no = "002";
    rows[1].txm_no = "B02";
    rows[1].chk_date = "2026-09-24 08:01:00";
    rows[2].id = "3";
    rows[2].oper_no = "005";
    rows[2].txm_no = "";
    rows[3].id = "4";
    rows[3].oper_no = "A2";
    rows[3].txm_no = "BA2";
    rows[4].id = "5";
    rows[4].oper_no = "A10";
    rows[4].txm_no = "BA10";
    rows[5] = rows[1];
    rows[5].id = "6";
    rows[5].chk_date = "2026-09-24 09:12:00";

    auto numeric = regular_barcode::select_range(rows, "1", "10");
    CHECK(numeric.error.empty());
    CHECK(numeric.candidates.size() == 4);
    CHECK(numeric.candidates[0].source_index == 1);
    CHECK(numeric.candidates[1].source_index == 5);
    CHECK(numeric.invalid_count == 1);
    CHECK(numeric.duplicate_count == 1);
    CHECK(!numeric.candidates[1].default_selected && numeric.candidates[1].duplicate);

    auto alpha = regular_barcode::select_range(rows, "A2", "A10");
    CHECK(alpha.error.empty() && alpha.candidates.size() == 2);
    CHECK(alpha.candidates[0].source_index == 3 && alpha.candidates[1].source_index == 4);
    CHECK(!regular_barcode::select_range(rows, "10", "2").error.empty());
    CHECK(!regular_barcode::select_range(rows, "", "2").error.empty());
    return 0;
}
