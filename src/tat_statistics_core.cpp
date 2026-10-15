#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace search {
namespace {
std::wstring expression_text(const std::string &text) {
    auto wide = utf8_to_wide(text);
    while (!wide.empty() && (wide.back() == L' ' || wide.back() == 0x3000))
        wide.pop_back();
    return wide;
}
#ifndef _WIN32
// Portable test oracle for the characters used in the marker. Windows uses NLS below.
void fold_width(std::wstring &text) {
    for (auto &c : text)
        if (c >= 0xff01 && c <= 0xff5e)
            c -= 0xfee0;
}
#endif
bool expression_equal(const std::string &a, const std::string &b) {
    auto left = expression_text(a), right = expression_text(b);
#ifdef _WIN32
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT,
                           NORM_IGNORECASE | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE, left.c_str(),
                           static_cast<int>(left.size()), right.c_str(),
                           static_cast<int>(right.size()), nullptr, nullptr, 0) == CSTR_EQUAL;
#else
    fold_width(left);
    fold_width(right);
    return left == right;
#endif
}

bool explicit_normal_marker(const std::string &text) {
    auto wide = expression_text(text);
    for (auto &c : wide)
        if (c >= 0xff01 && c <= 0xff5e)
            c -= 0xfee0;
    if (wide.size() < 4 || wide.substr(0, 2) != L"<=")
        return false;
    size_t i = 2;
    while (i < wide.size() && wide[i] >= L'0' && wide[i] <= L'9')
        ++i;
    if (i == 2)
        return false;
    const auto unit = wide.substr(i);
    return unit == L"分钟" || unit == L"小时" || unit == L"天";
}

long long timestamp_ms(std::string text) {
    text = trim(text);
    // SQL's varchar CREATE_TIME fallback may be formatted as "Sep  2 2026  8:31AM".
    if (text.size() < 19 || text[4] != '-') {
        char month[4]{}, suffix[3]{};
        int day = 0, year = 0, hour = 0, minute = 0;
        if (std::sscanf(text.c_str(), "%3s %d %d %d:%d%2s", month, &day, &year, &hour, &minute,
                        suffix) != 6)
            return -1;
        const std::string months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const auto pos = months.find(month);
        if (pos == std::string::npos || pos % 3 != 0 || hour < 1 || hour > 12 ||
            (std::string(suffix) != "AM" && std::string(suffix) != "PM"))
            return -1;
        hour = hour % 12 + (std::string(suffix) == "PM" ? 12 : 0);
        char canonical[32]{};
        std::snprintf(canonical, sizeof(canonical), "%04d-%02d-%02d %02d:%02d:00", year,
                      static_cast<int>(pos / 3 + 1), day, hour, minute);
        text = canonical;
    }
    if (text.size() < 19 || text.substr(0, 10) == "1900-01-01")
        return -1;
    const auto seconds = sql_datetime_diff_seconds("1900-01-01 00:00:00", text.substr(0, 19));
    if (seconds < 0)
        return -1;
    int fraction = 0;
    if (text.size() > 19) {
        if (text[19] != '.' || text.size() > 23 || text.size() == 20)
            return -1;
        for (size_t i = 20; i < text.size(); ++i) {
            if (text[i] < '0' || text[i] > '9')
                return -1;
            fraction = fraction * 10 + text[i] - '0';
        }
        for (size_t i = text.size(); i < 23; ++i)
            fraction *= 10;
    }
    return seconds * 1000 + fraction;
}
std::string compatibility_duration(long long ms, bool minutes_only) {
    if (minutes_only) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(5) << std::setw(11) << std::setfill('0')
            << static_cast<double>(ms) / 60000.0 << "分钟(分析)";
        return out.str();
    }
    const auto seconds = ms / 1000;
    std::string result;
    if (seconds / 86400)
        result += std::to_string(seconds / 86400) + "天";
    if (seconds / 3600 % 24)
        result += std::to_string(seconds / 3600 % 24) + "小时";
    if (seconds / 60 % 60)
        result += std::to_string(seconds / 60 % 60) + "分钟";
    if (seconds % 60)
        result += std::to_string(seconds % 60) + "秒(分析)";
    return result;
}
std::string rate(int count, int denominator) {
    if (denominator <= 0)
        return "-";
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << 100.0 * count / denominator << '%';
    return out.str();
}
std::string status(const TatStatDetailRow &row) {
    // A positive stored marker is known evidence even if another stage is incomplete.
    if (row.overtime)
        return "超时";
    if (row.time_abnormal)
        return "时间异常";
    if (row.missing_time)
        return "缺少时间";
    if (row.missing_marks)
        return "缺少统计标记";
    return "正常";
}
bool status_matches(TatFilter filter, const std::string &state) {
    switch (filter) {
    case TatFilter::all:
        return true;
    case TatFilter::normal:
        return state == "正常";
    case TatFilter::overtime:
        return state == "超时";
    case TatFilter::missing_time:
        return state == "缺少时间";
    case TatFilter::missing_marks:
        return state == "缺少统计标记";
    case TatFilter::abnormal:
        return state == "时间异常";
    }
    return false;
}
void count_status(const std::string &state, TatStatSummary &summary) {
    if (state == "超时")
        ++summary.overtime_count;
    else if (state == "正常")
        ++summary.normal_count;
    else if (state == "时间异常")
        ++summary.time_abnormal_count;
    else if (state == "缺少时间")
        ++summary.missing_time_count;
    else
        ++summary.missing_marks_count;
}
} // namespace

bool tat_is_overtime_marker(const std::string &value) {
    auto wide = expression_text(value);
#ifdef _WIN32
    if (wide.empty())
        return false;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,
                           FIND_ENDSWITH | NORM_IGNORECASE | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE,
                           wide.c_str(), static_cast<int>(wide.size()), L"(超时)", 4, nullptr,
                           nullptr, nullptr, 0) >= 0;
#else
    fold_width(wide);
    return wide.size() >= 4 && wide.substr(wide.size() - 4) == L"(超时)";
#endif
}

bool build_tat_statistics(const TatStatQuery &query, const std::vector<TatStatRawRow> &raw_rows,
                          TatStatSummary &summary, std::vector<TatStatDetailRow> &rows,
                          std::string &error) {
    rows.clear();
    summary = {};
    std::vector<std::string> orders;
    auto text = query.order_keyword;
    size_t position = 0;
    while ((position = text.find("、")) != std::string::npos)
        text.replace(position, 3, "\n");
    std::istringstream input(text);
    std::string order;
    while (std::getline(input, order))
        if (!trim(order).empty())
            orders.push_back(trim(order));
    for (const auto &raw : raw_rows) {
        if (query.emergency_only && !raw.report_emergency)
            continue;
        if (!orders.empty() && !raw.order_text_is_null &&
            std::none_of(orders.begin(), orders.end(), [&](const auto &value) {
                return expression_equal(raw.order_text, value);
            }))
            continue;
        TatStatDetailRow row;
        static_cast<TatStatRawRow &>(row) = raw;
        row.multiple_reports = raw.report_count > 1;
        row.normal_candidate = true;
        row.all_marks_null = true;
        for (size_t i = 0; i < 3; ++i) {
            const bool marked = raw.stored_times[i] && tat_is_overtime_marker(*raw.stored_times[i]);
            row.overtime |= marked;
            row.normal_candidate &= raw.stored_times[i].has_value() && !marked;
            row.all_marks_null &= !raw.stored_times[i].has_value();
            row.missing_marks |=
                !raw.stored_times[i] || (!marked && !explicit_normal_marker(*raw.stored_times[i]));
        }
        const std::array<std::string, 4> starts{
            {raw.collection_time, raw.receive_time, raw.collection_time, raw.receive_time}};
        const std::array<std::string, 4> ends{
            {raw.receive_time, raw.send_time, raw.send_time, raw.machine_time}};
        for (size_t i = 0; i < 4; ++i) {
            const auto start = timestamp_ms(starts[i]), end = timestamp_ms(ends[i]);
            if (i < 3) {
                row.missing_time |= start < 0 || end < 0;
                row.time_abnormal |= (start >= 0 && end >= 0 && end < start) ||
                                     (!trim(starts[i]).empty() && start < 0 &&
                                      trim(starts[i]).substr(0, 10) != "1900-01-01") ||
                                     (!trim(ends[i]).empty() && end < 0 &&
                                      trim(ends[i]).substr(0, 10) != "1900-01-01");
            }
            if (start >= 0 && end >= 0)
                row.duration_seconds[i] = std::llabs(end - start) / 1000;
            const auto original = raw.stored_times[i].value_or("");
            const bool minute_analysis = original.find("分钟(分析)") != std::string::npos;
            const bool retain =
                !original.empty() && (query.minutes_only ? minute_analysis : !minute_analysis);
            row.display_times[i] =
                retain
                    ? original
                    : (start < 0 || end < 0
                           ? "缺少时间(分析)"
                           : compatibility_duration(std::llabs(end - start), query.minutes_only));
        }
        if (row.multiple_reports)
            row.quality_note = "同条码有多份报告";
        const auto note = [&](const char *value) {
            if (!row.quality_note.empty())
                row.quality_note += "；";
            row.quality_note += value;
        };
        if (row.time_abnormal)
            note("时间顺序或格式异常");
        if (row.missing_time)
            note("主阶段时间不完整");
        if (row.missing_marks)
            note("日志缺少明确的正常或超时判定");
        row.tat_status = status(row);
        rows.push_back(std::move(row));
    }
    std::vector<TatStatDetailRow> unused;
    select_tat_statistics(TatMode::compatibility, TatFilter::all, rows, summary, unused);
    error.clear();
    return true;
}

void select_tat_statistics(TatMode mode, TatFilter filter,
                           const std::vector<TatStatDetailRow> &all_rows, TatStatSummary &summary,
                           std::vector<TatStatDetailRow> &visible) {
    summary = {};
    visible.clear();
    std::map<std::string, TatStatDetailRow> groups;
    std::map<std::string, std::array<bool, 3>> group_stages, group_stage_known;
    for (const auto &row : all_rows) {
        auto inserted = groups.emplace(row.barcode, row);
        auto &group = inserted.first->second;
        if (inserted.second)
            group_stage_known[row.barcode] = {{true, true, true}};
        if (!inserted.second) {
            group.overtime |= row.overtime;
            group.time_abnormal |= row.time_abnormal;
            group.missing_time |= row.missing_time;
            group.missing_marks |= row.missing_marks;
        }
        for (size_t i = 0; i < 3; ++i) {
            const bool overtime =
                row.stored_times[i] && tat_is_overtime_marker(*row.stored_times[i]);
            group_stages[row.barcode][i] |= overtime;
            group_stage_known[row.barcode][i] &=
                row.stored_times[i] && (overtime || explicit_normal_marker(*row.stored_times[i]));
        }
    }
    summary.total_count = static_cast<int>(groups.size());
    summary.detail_count = static_cast<int>(all_rows.size());
    if (mode == TatMode::improved) {
        for (const auto &group : groups)
            count_status(status(group.second), summary);
    } else {
        for (const auto &row : all_rows) {
            summary.overtime_count += row.overtime;
            summary.normal_count += row.normal_candidate;
            summary.time_abnormal_count += row.time_abnormal;
            summary.missing_time_count += row.missing_time;
            summary.missing_marks_count += row.missing_marks;
        }
    }
    std::set<std::string> visible_barcodes;
    for (const auto &row : all_rows) {
        bool include = true;
        if (mode == TatMode::compatibility) {
            if (filter == TatFilter::overtime)
                include = !row.normal_candidate && !row.all_marks_null;
            else if (filter == TatFilter::normal)
                include = !row.overtime;
            else if (filter == TatFilter::missing_time)
                include = row.missing_time;
            else if (filter == TatFilter::missing_marks)
                include = row.missing_marks;
            else if (filter == TatFilter::abnormal)
                include = row.time_abnormal;
        } else
            include = status_matches(filter, status(groups.at(row.barcode)));
        if (!include)
            continue;
        visible.push_back(row);
        if (mode == TatMode::improved) {
            visible.back().tat_status = status(groups.at(row.barcode));
            for (size_t i = 0; i < 4; ++i) {
                // Improved display has explicit zero/unknown values without changing stored
                // markers.
                if (row.stored_times[i] && !row.stored_times[i]->empty())
                    continue;
                visible.back().display_times[i] =
                    row.duration_seconds[i] < 0
                        ? "不可计算"
                        : format_duration_seconds_zh(row.duration_seconds[i]) + "（分析）";
            }
        }
        visible_barcodes.insert(row.barcode);
        if (mode == TatMode::compatibility)
            for (size_t i = 0; i < 3; ++i)
                summary.stage_overtime_count[i] +=
                    row.stored_times[i] && tat_is_overtime_marker(*row.stored_times[i]);
    }
    summary.visible_count = static_cast<int>(visible.size());
    summary.visible_barcode_count = static_cast<int>(visible_barcodes.size());
    summary.eligible_count = summary.normal_count + summary.overtime_count;
    if (mode == TatMode::improved) {
        summary.overtime_rate = rate(summary.overtime_count, summary.eligible_count);
        for (size_t i = 0; i < 3; ++i) {
            int eligible = 0;
            for (const auto &group : groups) {
                if (!group_stages[group.first][i] && !group_stage_known[group.first][i])
                    continue;
                ++eligible;
                summary.stage_overtime_count[i] += group_stages[group.first][i];
            }
            summary.stage_overtime_rate[i] = rate(summary.stage_overtime_count[i], eligible);
        }
    } else {
        if (!visible.empty() && summary.overtime_count > 0)
            summary.overtime_rate = rate(summary.overtime_count, summary.total_count);
        for (size_t i = 0; i < 3; ++i)
            if (!visible.empty() && summary.stage_overtime_count[i] > 0)
                summary.stage_overtime_rate[i] =
                    rate(summary.stage_overtime_count[i], summary.total_count);
    }
}

namespace {
const char *tat_projection = R"TATSQL(
SELECT DISTINCT
                b.OPER_CODE sign_oper ,
                BARCODE TXM_NO ,
                b.NAME ,
                b.IN_DIAGNOSIS AS DIAG_NAME ,
                b.DEPT_NAME AS DEPT_CODE ,
                b.REQ_DRN AS REQ_DR ,
                b.BEDNO AS BED_CODE ,
                b.TYPENAME AS TYPE ,
                b.REG_NO ,
                b.SEX AS SEX ,
                b.AGE ,
                b.SAMP_NAME AS BBMC ,
                b.ORDER_TEXT AS SQNR ,
                ISNULL(a.Apply_Time,b.REQ_TIME) AS SQRQ ,
            case b.TYPE when 1 then (  SELECT MAX(BBCJSJ) FROM yj_mzsq (NOLOCK) where  TXM=b.BARCODE  and BRXXID=b.PID )
	when 2 then ( SELECT TOP 1 y.REALEXECDATE FROM yj_zysq  x(NOLOCK) JOIN ZY_ORDEREXEC y (NOLOCK) ON x.INPATIENT_ID=y.INPATIENT_ID
            WHERE x.YZID=y.ORDER_ID AND x.ZXID =y.ID AND x.TXM=b.BARCODE ) end AS Collection_Time,
                ISNULL(a.Sign_Time,b.IN_DATE) AS SUB_DATE ,
                ISNULL(CREATE_TIME,CHK_DATE) CREATE_TIME ,
                 ISNULL(a.Send_Time,REP_TIME) REP_TIME ,
                a.Time_P AS Time_P ,
                a.Time_1 AS Time1 ,
                a.Time_2 AS Time2 ,
                a.Time_3 AS Time3 ,
                c.ROOM_NAME ,
                a.Time_4 AS Time4 ,
                a.Time_5 AS Time5 ,
                a.Time_6 AS Time6 ,
                a.Time_7 AS Time7 ,
                a.Time_8 AS Time8 ,
				a.Time_9 AS Time9 ,
                ( SELECT TOP 1 y.REALEXECDATE FROM yj_zysq  x(NOLOCK) JOIN ZY_ORDEREXEC y (NOLOCK) ON x.INPATIENT_ID=y.INPATIENT_ID
            WHERE x.YZID=y.ORDER_ID AND x.ZXID =y.ID AND x.TXM=b.BARCODE  ) Exec_Time,
                a.Transfer_Time ,
                case when x.assaypat_type=0 then 1 else 0 end as  jz_flag ,
                 CASE WHEN  ISNUMERIC(a.jjr)=1 THEN dbo.fun_getEmpName(a.jjr) ELSE a.jjr END  jjr ,
				    CASE WHEN  x.DEAN_OPER<>0 THEN dbo.fun_getEmpName(x.DEAN_OPER) ELSE CAST(x.DEAN_OPER AS VARCHAR(100)) END  shr ,
				 dbo.fun_getEmpName(x.oper_code) as JYZ,
                   ( SELECT TOP 1 dbo.fun_getempname(y.REALEXEUSER)  FROM yj_zysq  x(NOLOCK)
                   JOIN ZY_ORDEREXEC y (NOLOCK) ON x.INPATIENT_ID=y.INPATIENT_ID
                   WHERE x.YZID=y.ORDER_ID AND x.ZXID =y.ID AND x.TXM=b.BARCODE  )   cjr,
                b.oper_code AS qsr,
                (SELECT MIN(see_time)  FROM LIS_SEE_WJ_REPORT_LOG   (NOLOCK)
WHERE Rep_no IN (SELECT REP_NO FROM dbo.LS_AS_REPORT (NOLOCK) WHERE TXM_NO=a.TXM_NO AND delete_bit!=1))
AS SEE_TIME,
                a.Recive_Date AS ecjjsj,
    jep.NAME AS ecjjr
        FROM        LS_AS_BARCODE   b (NOLOCK) LEFT JOIN T_Lis_As_TATlog a   (NOLOCK)
               ON b.BARCODE = a.TXM_NO       AND b.DELETE_BIT != 1
                LEFT JOIN ls_as_report x (NOLOCK)  ON  b.BARCODE=x.TXM_NO
                LEFT JOIN LS_AS_ROOM c (NOLOCK)  ON c.ROOM_CODE = b.ROOM_CODE
                LEFT JOIN dbo.JC_EMPLOYEE_PROPERTY jep (NOLOCK)   ON jep.EMPLOYEE_ID=a.Recive_User
)TATSQL";
}

bool build_tat_sql_plan(const TatStatQuery &query, TatSqlPlan &plan, std::string &error) {
    plan = {};
    const auto valid_day = [](const std::string &date) {
        return date.size() == 10 &&
               sql_datetime_diff_seconds("1900-01-01 00:00:00", date + " 00:00:00") >= 0;
    };
    if (!valid_day(query.start_time) || !valid_day(query.end_time) ||
        query.start_time > query.end_time || query.end_time == "9999-12-31") {
        error = "请选择有效的签收起止日期";
        return false;
    }
    plan.sql = R"TATSQL(SELECT q.TXM_NO,q.TYPE,q.REG_NO,q.NAME,q.SEX,q.DIAG_NAME,q.BED_CODE,q.AGE,
q.BBMC,q.DEPT_CODE,q.ROOM_NAME,q.SQNR,
CONVERT(varchar(23),q.Collection_Time,121),CONVERT(varchar(23),q.SUB_DATE,121),q.CREATE_TIME,
CONVERT(varchar(23),q.REP_TIME,121),q.sign_oper,q.shr,q.Time1,q.Time2,q.Time3,q.Time9,q.jz_flag,
CASE WHEN origin.Sign_Time IS NULL THEN N'条码签收' ELSE N'TAT日志' END,
CASE WHEN origin.Send_Time IS NOT NULL THEN N'TAT日志' WHEN q.REP_TIME IS NOT NULL THEN N'报告回退' ELSE N'缺少时间' END,
reports.report_count
FROM (
)TATSQL";
    plan.sql += tat_projection;
    plan.sql += " WHERE b.ZT_FLAG<>9 AND b.DELETE_BIT=0 AND x.DELETE_BIT=0 AND x.CHK_FLAG<>'F'"
                " AND b.IN_DATE>=CONVERT(datetime,?,120)"
                " AND b.IN_DATE<DATEADD(day,1,CONVERT(datetime,?,120))";
    plan.parameters = {query.start_time, query.end_time};
    const auto add_code = [&](const char *column, const std::string &raw) {
        const auto value = trim(raw);
        if (value.empty() || value == "-1")
            return true;
        if (value.size() > 10 ||
            !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        plan.sql += std::string(" AND ") + column + "=?";
        plan.parameters.push_back(value);
        return true;
    };
    if (!add_code("b.DEPT_CODE", query.department_code) ||
        !add_code("b.ROOM_CODE", query.room_code) ||
        !add_code("b.sign_dept", query.sign_department_code)) {
        error = "科室和专业组须使用数字代码";
        plan = {};
        return false;
    }
    if (query.patient_type == "住院" || query.patient_type == "门诊") {
        plan.sql += " AND b.TYPE=?";
        plan.parameters.push_back(query.patient_type == "住院" ? "2" : "1");
    } else if (!query.patient_type.empty() && query.patient_type != "全部") {
        error = "病人类型无效";
        plan = {};
        return false;
    }
    const auto group = trim(query.order_group_id);
    if (!group.empty() && group != "-1") {
        if (group.size() > 10 ||
            !std::all_of(group.begin(), group.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            error = "医嘱组须使用数字代码";
            plan = {};
            return false;
        }
        plan.sql += " AND b.HOITEM_ID IN(SELECT YzID FROM T_Lis_As_TatTimeSet WITH(NOLOCK) WHERE "
                    "YzGroupID=?)";
        plan.parameters.push_back(group);
    }
    plan.sql += R"TATSQL(
) q LEFT JOIN T_Lis_As_TATlog origin WITH(NOLOCK) ON origin.TXM_NO=q.TXM_NO
OUTER APPLY (SELECT COUNT(DISTINCT r.REP_NO) report_count FROM LS_AS_REPORT r WITH(NOLOCK)
             WHERE r.TXM_NO=q.TXM_NO AND r.DELETE_BIT=0 AND r.CHK_FLAG<>'F') reports
ORDER BY q.SUB_DATE
)TATSQL";
    error.clear();
    return true;
}
} // namespace search
