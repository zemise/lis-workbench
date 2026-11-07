#include "barcode_report_matching.h"
#include "barcode_report_query.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace search;
namespace {
void check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
BarcodeMatchSource source() { return {"old-barcode", "patient-X", "2", "10", "20", "30", "40", "2026-11-06 08:00:00"}; }
std::vector<BarcodeExpectedItem> definitions() { return {{"30","10","20","1"}, {"30","10","20","2"}}; }
BarcodeMatchReport report() {
    BarcodeMatchReport r;
    r.fields.barcode="new-barcode"; r.fields.reg_no="patient-X"; r.patient_type="2";
    r.fields.machine_code="10"; r.fields.room_code="20";
    r.fields.report_no="900"; r.fields.inspect_date="2026-11-06 00:00:00";
    r.fields.review_time="2026-11-06 09:00:00"; r.fields.sample_no="100";
    r.fields.tester="tester"; r.fields.reviewer="reviewer"; r.fields.machine_name="instrument";
    r.sent=true; r.reviewed=true; r.items={"1","2","3"}; r.result_items=r.items;
    return r;
}
BarcodeMatchResult match(BarcodeMatchSource s, std::vector<BarcodeMatchReport> reports,
                         std::vector<BarcodeExpectedItem> defs=definitions(),
                         std::vector<BarcodeOrderMapping> maps={}) {
    return BarcodeReportMatcher(defs,reports).match(s,barcode_match_profiles(s,maps));
}
void matching_cases() {
    auto s=source(); auto r=report();
    check(match(s,{r}).report_index==0,"covered merged report");
    auto x=r; x.fields.reg_no="other"; check(!match(s,{x}).report_index,"patient isolation");
    x=r; x.patient_type="1"; check(!match(s,{x}).report_index,"patient type isolation");
    x=r; x.fields.machine_code="11"; check(!match(s,{x}).report_index,"instrument isolation");
    x=r; x.fields.room_code="21"; check(!match(s,{x}).report_index,"room isolation");
    x=r; x.fields.inspect_date="2026-11-07 00:00:00"; check(!match(s,{x}).report_index,"fixed day window");
    // The result for an older source cannot change when a multi-day query adds more reports.
    check(match(s,{r,x}).report_index==0,"multi-day invariant");
    s.receive_time="2026-11-07 08:00:00"; x.fields.review_time="2026-11-07 09:00:00";
    check(match(s,{r,x}).report_index==1,"separate source-day key");
    s=source(); x=r; x.deleted=true; check(!match(s,{x}).report_index,"deleted report");
    x=r; x.fields.barcode=""; check(!match(s,{x}).report_index,"unbound report");
    x=r; x.fields.barcode=s.barcode; check(!match(s,{x}).report_index,"direct barcode excluded");
    x=r; x.items={"1"}; check(!match(s,{x}).report_index,"all expected items required");
    x=r; x.result_items={"1"}; check(match(s,{x}).reason==BarcodeMatchReason::incomplete_results,"sent but incomplete results");
    x.sent=false; x.reviewed=false; check(match(s,{x}).report_index==0,"registered unreviewed report");
    x=r; x.fields.review_time="2026-11-06 07:59:59";
    check(match(s,{x}).reason==BarcodeMatchReason::invalid_time,"earlier reviewed specimen counterexample");
    x.fields.review_time=""; check(match(s,{x}).reason==BarcodeMatchReason::invalid_time,"missing review timestamp");
    x.fields.review_time=s.receive_time; check(match(s,{x}).report_index==0,"equal timestamp boundary");
    x=r; x.fields.report_no="901";
    check(match(s,{r,x}).reason==BarcodeMatchReason::ambiguous,"do not choose latest report");
    check(match(s,{r,r}).report_index==0,"duplicate join rows are not multiple reports");
    x=r; x.items={"1"}; auto y=r; y.fields.report_no="902"; y.items={"2"};
    check(!match(s,{x,y}).report_index,"never union separate reports");
    check(match(s,{r},{}).reason==BarcodeMatchReason::missing_items,"missing dictionary");
    s.patient_no=" "; check(match(s,{r}).reason==BarcodeMatchReason::missing_identity,"empty identity");
    s=source(); s.receive_time="2026-02-30 08:00:00";
    check(match(s,{r}).reason==BarcodeMatchReason::missing_date,"impossible date");
    s.receive_time="2026-11-06"; check(!match(s,{r}).report_index,"date without sign time is insufficient");
    check(barcode_numeric_id(" 0010 ")=="10" && barcode_numeric_id("10 OR 1=1").empty(),"numeric identifiers");
}
void mapping_cases() {
    auto s=source(); s.machine=""; s.group="";
    std::vector<BarcodeOrderMapping> maps={{"40","30","10","20"},{"40","31","10","20"},{"40","99","50","99"}};
    auto p=barcode_match_profiles(s,maps);
    check(p.size()==1 && p[0].groups.size()==2,"derive and union groups only within source room");
    auto d=definitions(); d.push_back({"31","10","20","3"});
    check(match(s,{report()},d,maps).report_index==0,"derived complete multi-group coverage");
    auto r=report(); r.items.erase("3"); check(!match(s,{r},d,maps).report_index,"derived missing group project");
    check(match(s,{report()},definitions(),maps).reason==BarcodeMatchReason::missing_items,"unknown group must not shrink required set");
    s.order_code="invalid"; check(match(s,{report()},d,maps).reason==BarcodeMatchReason::missing_mapping,"invalid order code");
}
void presentation_cases() {
    BarcodeQueryRow row; row.barcode="old-barcode"; row.order_text="original order";
    row.receive_time=source().receive_time; row.machine_status="已上机未审核";
    BarcodeQueryFilters f; f.machine_statuses={"发送完成"};
    check(!barcode_status_matches(row,f),"status before association");
    apply_barcode_report_match(row,report());
    check(barcode_status_matches(row,f),"filter after association includes recovered sent row");
    f.machine_statuses={"已上机未审核","已签收未上机"};
    check(!barcode_status_matches(row,f),"unfinished excludes recovered sent row");
    f.machine_statuses.clear(); f.machine_status="全部"; check(barcode_status_matches(row,f),"all status");
    f.machine_status="obsolete setting"; check(barcode_status_matches(row,f),"unknown legacy filter does not hide everything");
    f.machine_statuses={"全部","已上机未审核"};
    check(!barcode_status_matches(row,f),"match existing SQL multiselect semantics");
    check(row.barcode=="old-barcode" && row.order_text=="original order","keep original specimen/order identity");
    check(row.report_no=="900" && row.sample_no=="100" && row.machine_name=="instrument" &&
          row.tester=="tester" && row.reviewer=="reviewer" && row.review_elapsed_seconds==3600 &&
          row.report_link=="项目匹配关联","consistent target, staff, timing and provenance");
}
}
int main() {
    try {
        matching_cases(); mapping_cases(); presentation_cases();
#ifdef _WIN32
        BarcodeQueryFilters filters;
        std::vector<BarcodeMatchSource> sources{source()};
        std::vector<BarcodeQueryRow> rows(1);
        rows[0].machine_status="已签收未上机";
        std::string error;
        // A null connection makes any accidental SQL execution fail this test.
        check(!supplement_barcode_reports(SQL_NULL_HDBC,filters,sources,rows,error,[]{return true;}),"cancel before SQL");
        check(!error.empty(),"cancel must not silently succeed");
        filters.canceled=true;error.clear();
        check(supplement_barcode_reports(SQL_NULL_HDBC,filters,sources,rows,error,{}),"cancelled receipt needs no inference SQL");
        check(rows[0].machine_status=="已签收未上机" && rows[0].report_link=="取消签收：未补关联","cancelled receipt preserves original status");
#endif
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    std::cout<<"barcode report matching tests passed\n";
}
