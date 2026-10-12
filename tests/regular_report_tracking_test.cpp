#include "regular_report_tracking.h"
#include <cstdlib>
#include <iostream>

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    regular_tracking::Options o;
    o.connection = "test-db"; o.patient = "000123";
    o.origin_room = "room"; o.origin_machine = "machine"; o.today = "2026-10-11";
    search::QueryFilters f; std::string error;
    check(regular_tracking::build_filters(o, f, error), "default query valid");
    check(f.start_date == "2026-09-12" && f.end_date == "2026-10-11", "30 days include today");
    check(f.patient_id == "000123" && f.patient_no.empty(), "patient matches report REG_NO directly; keep leading zeros");
    check(f.room_code.empty() && f.mach_code.empty() && f.report_status.empty(), "default all machines and statuses");
    check(f.limit == 0 && f.skip_order_text && f.connection_string == "test-db", "no report cap, no order aggregation, current database");
    o.current_machine = true; o.reviewed = true;
    check(regular_tracking::build_filters(o, f, error) && f.mach_code.empty() && f.room_code.empty() &&
          f.report_status.empty(), "database snapshot always includes all machines and statuses, even with boxes checked");
    o.current_machine = false; o.reviewed = false;
    check(regular_tracking::build_filters(o, f, error) && f.mach_code.empty() && f.room_code.empty() &&
          f.report_status.empty(), "unchecking filters clears previous machine and review restrictions");
    o.current_machine = true;
    o.today = "2024-03-01"; o.days = 3;
    check(regular_tracking::build_filters(o, f, error) && f.start_date == "2024-02-28", "leap day");
    o.today = "2026-03-01";
    check(regular_tracking::build_filters(o, f, error) && f.start_date == "2026-02-27", "non-leap February");
    o.today = "2026-01-02";
    check(regular_tracking::build_filters(o, f, error) && f.start_date == "2025-12-31", "year boundary");
    o.today = "2026-03-09"; o.days = 2;
    check(regular_tracking::build_filters(o, f, error) && f.start_date == "2026-03-08", "natural days across DST boundary");
    o.days = 1;
    check(regular_tracking::build_filters(o, f, error) && f.start_date == f.end_date, "one day only today");
    o.days = 9999;
    check(regular_tracking::build_filters(o, f, error) && f.start_date == "1998-10-24", "maximum days");
    for (int n : {0, -1, 10000}) {
        o.days = n; check(!regular_tracking::build_filters(o, f, error) && !error.empty(), "reject invalid day count");
    }
    o.days = 30;
    for (const char* date : {"", "2026-02-29", "2024-02-30", "2026-13-01", "2026-00-01", "2026-01-00", "2026-10-11x", "2026-1a-11"}) {
        o.today = date; check(!regular_tracking::build_filters(o, f, error), "reject invalid dates");
    }
    o.today = "2026-10-11"; o.patient = " ";
    check(!regular_tracking::build_filters(o, f, error), "never query without patient identity");
    o.patient = "123"; o.origin_machine.clear();
    check(regular_tracking::build_filters(o, f, error), "missing origin instrument does not prevent full snapshot query");
    o.current_machine = false;
    check(regular_tracking::build_filters(o, f, error), "all instruments allowed without origin instrument");
    std::vector<search::ReportRow> snapshot(5);
    for (size_t i = 0; i < snapshot.size(); ++i) {
        auto& r = snapshot[i]; r.rep_no = std::to_string(i + 1);
        r.mach_code = "machine"; r.room_code = "room"; r.chk_flag = "T";
    }
    snapshot[1].chk_flag = ""; snapshot[1].conf = "T";
    snapshot[2].mach_code = "other";
    snapshot[3].room_code = "other-room";
    snapshot[4].mach_code = " machine "; snapshot[4].room_code = " room "; snapshot[4].chk_flag = " T ";
    o.origin_machine = "machine"; o.origin_room = "room";
    o.current_machine = true; o.reviewed = true;
    auto visible = regular_tracking::filter_reports(snapshot, o);
    check(visible.size() == 2 && visible[0].rep_no == "1" && visible[1].rep_no == "5",
          "combined local filters use origin machine plus room and CHK_FLAG, preserve sort order and trim CHAR padding");
    o.reviewed = false;
    check(regular_tracking::filter_reports(snapshot, o).size() == 3, "machine filter includes unreviewed reports");
    o.current_machine = false; o.reviewed = true;
    check(regular_tracking::filter_reports(snapshot, o).size() == 4, "review filter includes other machines and rooms");
    for (int i = 0; i < 5; ++i) {
        o.current_machine = true;
        check(regular_tracking::filter_reports(snapshot, o).size() == 2, "repeated checkbox selection");
        o.current_machine = false; o.reviewed = false;
        visible = regular_tracking::filter_reports(snapshot, o);
        check(visible.size() == 5 && visible[2].rep_no == "3" && snapshot.size() == 5,
              "unchecking restores full snapshot without discarding hidden reports");
        o.reviewed = true;
    }
    o.current_machine = true; o.origin_machine.clear();
    check(regular_tracking::filter_reports(snapshot, o).empty(), "missing origin instrument never broadens local filter");
    o.origin_machine = "machine"; o.origin_room.clear();
    check(regular_tracking::filter_reports(snapshot, o).size() == 3, "no origin room matches machine only");
    check(regular_tracking::filter_reports({}, o).empty(), "empty snapshot stays empty");
    std::cout << "Tracking date and local filter tests passed\n";
}
