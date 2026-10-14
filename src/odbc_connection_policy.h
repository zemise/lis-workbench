#pragma once
#include "app_settings.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// Shared by normal LIS queries and dedicated update connections. No SQL execution.
namespace search::odbc {
inline std::string trim(std::string value) {
    const auto not_space=[](unsigned char c){return !std::isspace(c);};
    value.erase(value.begin(),std::find_if(value.begin(),value.end(),not_space));
    value.erase(std::find_if(value.rbegin(),value.rend(),not_space).base(),value.end());
    return value;
}
inline std::string upper_ascii(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::toupper(c));});
    return value;
}
inline std::vector<std::string> split(const std::string& value,char delimiter) {
    std::vector<std::string> parts; std::istringstream input(value); std::string part;
    while(std::getline(input,part,delimiter)) parts.push_back(part);
    return parts;
}
inline std::wstring query_connection_string(const DbSettings& settings) {
    if(settings.server.empty() || settings.initial_database.empty() || settings.user.empty()) return {};
    return L"packet size=4096;user id="+settings.user+L";password="+settings.password+
        L";data source="+settings.server+L";persist security info=True;initial catalog="+settings.initial_database;
}
inline std::map<std::string, std::string> parse_connection_kv(const std::string& text) {
    std::map<std::string, std::string> values;
    for (const auto& part : split(text, ';')) {
        const auto pos = part.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[upper_ascii(trim(part.substr(0, pos)))] = trim(part.substr(pos + 1));
    }
    return values;
}

inline std::vector<std::string> odbc_candidates(const std::string& input) {
    if (input.find("DRIVER=") != std::string::npos || input.find("Driver=") != std::string::npos) {
        return {input};
    }
    const auto kv = parse_connection_kv(input);
    const auto server_it = kv.find("DATA SOURCE");
    const auto db_it = kv.find("INITIAL CATALOG");
    const auto user_it = kv.find("USER ID");
    const auto pass_it = kv.find("PASSWORD");
    if (server_it == kv.end() || db_it == kv.end() || user_it == kv.end() || pass_it == kv.end()) {
        return {input};
    }

    return {
        "DRIVER={ODBC Driver 18 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={ODBC Driver 17 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";",
    };
}

inline std::mutex& preferred_odbc_candidate_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::map<std::string, std::string>& preferred_odbc_candidates() {
    static std::map<std::string, std::string> cache;
    return cache;
}

inline std::string cached_odbc_candidate(const std::string& connection_string) {
    std::lock_guard<std::mutex> lock(preferred_odbc_candidate_mutex());
    const auto it = preferred_odbc_candidates().find(connection_string);
    return it == preferred_odbc_candidates().end() ? std::string{} : it->second;
}

inline void remember_odbc_candidate(const std::string& connection_string, const std::string& candidate) {
    std::lock_guard<std::mutex> lock(preferred_odbc_candidate_mutex());
    preferred_odbc_candidates()[connection_string] = candidate;
}

inline std::vector<std::string> prioritized_odbc_candidates(const std::string& connection_string,
                                                     std::string& cached_candidate) {
    auto candidates = odbc_candidates(connection_string);
    cached_candidate = cached_odbc_candidate(connection_string);
    if (cached_candidate.empty()) {
        return candidates;
    }
    candidates.erase(std::remove(candidates.begin(), candidates.end(), cached_candidate), candidates.end());
    candidates.insert(candidates.begin(), cached_candidate);
    return candidates;
}

// Driver names are fixed candidates; callers never expose the connection string.
inline std::string candidate_driver(const std::string& candidate) {
    const auto end=candidate.find('}');
    return candidate.rfind("DRIVER={",0)==0 && end!=std::string::npos ? candidate.substr(8,end-8) : "ODBC";
}
}  // namespace search::odbc
