#include "log.h"
#include "search_core.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>

int main() {
    namespace fs = std::filesystem;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / ("lis-log-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directory(root);
    std::string messages, error;
    std::vector<search::ReportRow> rows;
    search::QueryFilters filters;
    filters.connection_string = "DRIVER={LIS_TEST_MISSING_SECRET};UID=TEST_PATIENT;PWD=TEST_PASSWORD;";
    if (search::query_reports(filters, rows, error,
            [&](const std::string& text) { messages += text; })) return 3;
    for (const auto* secret : {"LIS_TEST_MISSING_SECRET", "TEST_PATIENT", "TEST_PASSWORD"}) {
        if (messages.find(secret) != std::string::npos) return 4;
    }
    if (messages.find("db connect failed") == std::string::npos) return 5;
    const auto old = root / "2000-01-01.log";
    std::ofstream(old) << "old";
    fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(24 * 20));
    std::ofstream(root / "keep.txt") << "unrelated";
    applog::init(root.wstring(), 14, 4096);
    if (fs::exists(old)) return 1;
    for (int i = 0; i < 100; ++i) LOG_INFO("query=reports event=execute rows=12");
    unsigned long long total = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().extension() == ".log") total += entry.file_size();
    }
    if (total == 0 || total > 4096 || !fs::exists(root / "keep.txt")) return 2;
    fs::remove_all(root);
    std::cout << "log retention and capacity tests passed\n";
}
