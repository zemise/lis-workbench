#pragma once

#include <functional>
#include <string>
#include <vector>

namespace search {

struct QueryFilters {
    std::string connection_string;
    std::string patient_id;
    std::string barcode;
    std::string patient_name;
    std::string patient_no;
    std::string oper_no;
    std::string start_date;
    std::string end_date;
    std::string room_code;
    std::string patient_type;
    std::string report_status;
    std::string mach_code;
    std::string group_code;
    std::string item_code;
    std::string lis_abo_codes;
    std::string lis_rhd_codes;
    std::string lis_hgb_codes;
    std::string lis_plt_codes;
    int limit = 300;
};

struct ReportRow {
    std::string rep_no;
    std::string oper_no;
    std::string name;
    std::string txm_no;
    std::string chk_date;
    std::string sex;
    std::string age;
    std::string bed_code;
    std::string patient_type;
    std::string requester;
    std::string reviewer;
    std::string group_name;
    std::string conf;
    std::string chk_flag;
    std::string zymz_print;
    std::string zzj_print;
    std::string reg_no;
};

struct RoomOption {
    std::string room_code;
    std::string room_name;
};

struct PatientTypeOption {
    std::string type_code;
    std::string type_name;
};

struct MachineOption {
    std::string mach_code;
    std::string mach_name;
};

struct ResultRow {
    std::string item_name;
    std::string result;
    std::string downbound;
    std::string upbound;
    std::string unit;
    std::string item_eng;
    std::string normal;
    std::string item_code;
};

struct LisSummary {
    std::string abo;
    std::string rhd;
    std::string blood_type_date;
    std::string hgb;
    std::string plt;
    std::string cbc_date;
};

struct BloodRequestRow {
    std::string tran_property;   // 输血性质/备血类型（TranProperty）
    std::string patient_name;    // 姓名
    std::string apply_dept;      // 申请科室
    std::string apply_bed_no;    // 备血（床号）
    std::string apply_abo;       // 申请ABO
    std::string apply_rhd;       // 申请RHD
    std::string apply_composition; // 申请成分
    std::string apply_form_no;   // 申请单号
    std::string check_doctor;    // 审核人
    std::string check_date;      // 审核时间
    std::string apply_status;    // 申请状态
    std::string patient_no;      // 病人编号（内部）
    std::string apply_time;      // 申请日期（内部）
    std::string urgency_level;   // 紧急程度（UrgencyLevel，右侧详情用）
    std::string transfusion_history; // 输血史（reactionHistory）
    std::string patient_no_type; // 病人类型（Patient_NOType）
    std::string patient_sex;     // 性别（Patient_Sex）
    std::string patient_age;     // 年龄（Patient_Age + Patient_AgeUnit）
    std::string reaction_history; // 反应史（FYS）
};

struct BloodQueryFilters {
    std::string connection_string;
    std::string patient_no;      // 病人编号
    std::string patient_name;    // 病人姓名
    std::string apply_form_no;   // 申请单号
    std::string apply_status;    // 申请状态
    std::string start_date;      // 开始日期
    std::string end_date;        // 结束日期
    int limit = 500;
};

using LogFn = std::function<void(const std::string&)>;

bool query_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log = {});
bool query_patient_types(const std::string& connection_string, std::vector<PatientTypeOption>& rows, std::string& error, LogFn log = {});
bool query_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log = {});
bool query_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log = {});
bool query_results(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error, LogFn log = {});
bool query_lis_summary(const QueryFilters& filters, LisSummary& summary, std::string& error, LogFn log = {});
bool query_blood_requests(const BloodQueryFilters& filters, std::vector<BloodRequestRow>& rows, std::string& error, LogFn log = {});

}  // namespace search
