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

using LogFn = std::function<void(const std::string&)>;

bool query_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log = {});
bool query_patient_types(const std::string& connection_string, std::vector<PatientTypeOption>& rows, std::string& error, LogFn log = {});
bool query_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log = {});
bool query_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log = {});
bool query_results(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error, LogFn log = {});

}  // namespace search
