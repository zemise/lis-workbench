#include "search_core.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
search::TatStatRawRow base() {
    search::TatStatRawRow r;
    r.barcode = "sample";
    r.patient_type = "门诊";
    r.order_text = "肝功能";
    r.collection_time = "2026-09-02 08:24:00.000";
    r.receive_time = "2026-09-02 08:26:00.000";
    r.machine_time = "2026-09-02 08:31:00";
    r.send_time = "2026-09-02 10:26:00.000";
    r.stored_times = {"<=30分钟", "<=180分钟", "<=240分钟", std::nullopt};
    r.report_count = 2;
    r.receive_source = "条码签收";
    r.send_source = "报告回退";
    return r;
}
} // namespace
int main() {
    search::TatStatQuery q;
    search::TatStatSummary summary;
    std::vector<search::TatStatDetailRow> rows, visible;
    std::string error;
    auto a = base();
    a.stored_times[1] = ">180分钟(超时)";
    auto b = a;
    b.order_text = "肾功能";
    check(search::build_tat_statistics(q, {a, b}, summary, rows, error), "build");
    check(rows.size() == 2 && rows[0].order_text == "肝功能",
          "preserve distinct detail rows, no barcode aggregation");
    check(rows[0].multiple_reports && !rows[0].quality_note.empty(), "multiple report warning");
    check(rows[0].send_source == "报告回退", "preserve provenance");
    search::select_tat_statistics(search::TatMode::compatibility, search::TatFilter::all, rows,
                                  summary, visible);
    check(summary.total_count == 1 && summary.detail_count == 2 && summary.overtime_count == 2,
          "compatibility row numerator / barcode denominator");
    check(summary.overtime_rate == "200.00%", "compatibility rate may exceed 100");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::all, rows, summary,
                                  visible);
    check(summary.total_count == 1 && summary.overtime_count == 1 &&
              summary.overtime_rate == "100.00%",
          "improved barcode numerator");
    check(visible.size() == 2, "improved keeps evidence details");

    // Independent 64-case truth table: DBNull is unknown, empty string is a non-null value.
    const std::optional<std::string> values[] = {std::nullopt, "", "<=30分钟", ">30分钟(超时)"};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) {
                auto r = base();
                r.stored_times[0] = values[i];
                r.stored_times[1] = values[j];
                r.stored_times[2] = values[k];
                check(search::build_tat_statistics(q, {r}, summary, rows, error), "matrix build");
                const bool overtime = i == 3 || j == 3 || k == 3;
                const bool normal = i != 0 && j != 0 && k != 0 && !overtime;
                const bool absent = i == 0 && j == 0 && k == 0;
                search::select_tat_statistics(search::TatMode::compatibility,
                                              search::TatFilter::overtime, rows, summary, visible);
                check(visible.size() == static_cast<size_t>(!normal && !absent),
                      "standard compatibility overtime deletion matrix");
                search::select_tat_statistics(search::TatMode::compatibility,
                                              search::TatFilter::normal, rows, summary, visible);
                check(visible.size() == static_cast<size_t>(!overtime),
                      "standard compatibility normal deletion matrix");
            }
    check(search::tat_is_overtime_marker(">30分钟(超时)　 "), "spaces ignored");
    check(search::tat_is_overtime_marker(">30分钟（超时）"), "width-insensitive suffix");
    check(!search::tat_is_overtime_marker(">30分钟(超时)\t"), "TAB retained");
    check(!search::tat_is_overtime_marker(">30分钟(超时)其他"), "not substring matching");

    a = base();
    a.stored_times = {"2分钟", "2小时1秒(分析)", "2小时", std::nullopt};
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "unclassified analysis text");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::all, rows, summary,
                                  visible);
    check(summary.normal_count == 0 && summary.missing_marks_count == 1,
          "analysis text is not a proven normal result");
    check(summary.stage_overtime_rate[0] == "-", "unknown stage excluded from stage denominator");

    a = base();
    a.stored_times[1] = ">180分钟(超时)";
    q.minutes_only = true;
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "minutes-only configuration");
    check(rows[0].display_times[1] == "00120.00000分钟(分析)" && rows[0].overtime,
          "analysis formatting must not rewrite raw overtime classification");
    q.minutes_only = false;

    a = base();
    a.stored_times = {};
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "analysis only");
    check(rows[0].duration_seconds[0] == 120 && rows[0].display_times[0] == "2分钟",
          "exact compatibility minute display");
    check(rows[0].display_times[1] == "2小时", "omit zero components / suffix");
    check(rows[0].missing_marks && !rows[0].overtime,
          "time differences do not manufacture overtime");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::all, rows, summary,
                                  visible);
    check(summary.missing_marks_count == 1 && summary.normal_count == 0 &&
              summary.overtime_rate == "-",
          "unknown excluded from eligible samples");
    a.receive_time = a.collection_time;
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "zero build");
    check(rows[0].display_times[0].empty(), "compatibility zero stays empty");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::all, rows, summary,
                                  visible);
    check(visible[0].display_times[0] == "0秒（分析）", "improved zero explicit");
    a = base();
    a.collection_time.clear();
    a.send_time.clear();
    a.stored_times = {};
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "missing build");
    check(rows[0].duration_seconds[0] < 0 && rows[0].display_times[0] == "缺少时间(分析)",
          "missing never uses current time");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::normal, rows,
                                  summary, visible);
    check(visible.empty() && summary.missing_time_count == 1, "incomplete is not normal");
    a = base();
    a.stored_times = {};
    a.send_time = "2026-09-02 08:25:59.100";
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "reverse build");
    check(rows[0].time_abnormal, "reverse quality flag even with compatibility absolute display");
    a = base();
    a.stored_times = {};
    a.collection_time = "2026-09-02 08:25:59.900";
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "subsecond build");
    check(rows[0].duration_seconds[0] == 0, "fractional difference before truncation");
    a = base();
    a.machine_time = "Sep  2 2026  8:31AM";
    a.stored_times = {};
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "SQL varchar fallback");
    check(rows[0].duration_seconds[3] == 300, "parse SQL style-zero fallback");
    a = base();
    check(search::build_tat_statistics(q, {a}, summary, rows, error), "normal build");
    search::select_tat_statistics(search::TatMode::compatibility, search::TatFilter::all, rows,
                                  summary, visible);
    check(summary.overtime_rate == "-", "standard compatibility zero overtime dash");
    search::select_tat_statistics(search::TatMode::improved, search::TatFilter::all, rows, summary,
                                  visible);
    check(summary.overtime_rate == "0.00%", "improved zero rate");
    q.order_keyword = "肾功能";
    check(search::build_tat_statistics(q, {a}, summary, rows, error) && rows.empty(),
          "exact order filter");
    a.order_text_is_null = true;
    check(search::build_tat_statistics(q, {a}, summary, rows, error) && rows.size() == 1,
          "standard compatibility NULL order survives NOT IN removal");

    q = {};
    q.start_time = "2026-09-01";
    q.end_time = "2026-09-02";
    q.department_code = "42";
    q.sign_department_code = "102";
    q.room_code = "3";
    q.order_group_id = "5";
    q.patient_type = "住院";
    search::TatSqlPlan plan;
    check(search::build_tat_sql_plan(q, plan, error), "parameterized plan");
    check(plan.parameters ==
              std::vector<std::string>({"2026-09-01", "2026-09-02", "42", "3", "102", "2", "5"}),
          "binding order");
    check(plan.sql.find("SELECT DISTINCT") != std::string::npos &&
              plan.sql.find("TOP 1 r.REP_NO") == std::string::npos,
          "original row identity, no representative report");
    check(plan.sql.find("b.ZT_FLAG<>9") != std::string::npos &&
              plan.sql.find("x.CHK_FLAG<>'F'") != std::string::npos,
          "original report inclusion contract");
    check(plan.sql.find("b.COLLECTION_TIME") == std::string::npos &&
              plan.sql.find("MAX(BBCJSJ)") != std::string::npos,
          "collection is application/execution source");
    check(plan.sql.find("CANCEL_DATE IS NULL") == std::string::npos,
          "do not add cancellation exclusion");
    q.department_code = "1';DELETE FROM LS_AS_BARCODE--";
    check(!search::build_tat_sql_plan(q, plan, error) && plan.sql.empty(),
          "reject untrusted codes");
    q.department_code = "";
    q.end_time = "2026-02-30";
    check(!search::build_tat_sql_plan(q, plan, error), "reject invalid calendar date");
    std::cout << "tat statistics tests passed\n";
}
