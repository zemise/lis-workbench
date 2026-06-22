#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace qc {

struct Config {
    int id = 0;
    bool enabled = true;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string qc_name;
    std::string level;
    std::string item_code;
    std::string item_name;
    std::string target_mean;
    std::string target_sd;
};

struct Result {
    int id = 0;
    std::string source_rep_no;
    std::string source_entry_key;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string tester_name;
    std::string report_date;
    std::string inspect_date;
    std::string report_time;
    std::string effective_time;
    std::string item_code;
    std::string item_name;
    std::string result_text;
    double result_value = 0.0;
    bool has_numeric_value = false;
    std::string unit;
    std::string normal;
    std::string qc_name;
    std::string level;
    std::string target_mean;
    std::string target_sd;
    double qc_mean = 0.0;
    double qc_sd = 0.0;
    double qc_z = 0.0;
    bool has_qc_stats = false;
    bool has_qc_z = false;
    std::string qc_status;
    std::string qc_rules;
};

struct Query {
    std::string start_date;
    std::string end_date;
    std::string mach_code;
    std::string item_code;
    std::string level;
};

std::wstring default_db_path();

bool ensure_store(std::string& error);
bool load_configs(std::vector<Config>& rows, std::string& error);
bool save_config(Config& row, std::string& error);
bool delete_config(int id, std::string& error);

}  // namespace qc

#endif
