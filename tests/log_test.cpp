#include "log.h"
#include "search_core.h"
#include "search_controller.h"
#include "trend_core.h"
#include "query_diagnostics.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <stdexcept>


namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

#ifdef _WIN32
int reader_calls = 0;
SQLRETURN SQL_API fake_diagnostic(SQLSMALLINT, SQLHANDLE, SQLSMALLINT record,
                                  SQLWCHAR* state, SQLINTEGER* native, SQLWCHAR* message,
                                  SQLSMALLINT capacity, SQLSMALLINT* length) {
    ++reader_calls;
    if (record > 1) return SQL_NO_DATA;
    const wchar_t* code = L"HYT00";
    for (int i = 0; i < 6; ++i) state[i] = code[i];
    *native = -2;
    const std::wstring sensitive = L"PWD=TEST_PASSWORD;NAME=TEST_PATIENT;SELECT secret FROM patient;";
    *length = static_cast<SQLSMALLINT>(sensitive.size());
    require(capacity == 1, "production collector requests no provider text");
    message[0] = 0;
    return SQL_SUCCESS_WITH_INFO;
}
#endif

bool diagnostic_checks(const std::filesystem::path& root) {
    using querydiag::Operation;
    const auto authentication = querydiag::safe_error(Operation::connect,
        {{"01000", 0}, {"28000", 18456}});
    require(authentication.find("[DB_AUTH]") == 0, "login classification across multiple records");
    require(authentication.find("SQLSTATE=28000,native=18456") != std::string::npos,
        "safe SQLSTATE and native code retained");
    const auto timeout = querydiag::safe_error(Operation::execute, {{"HYT00", -2}});
    require(timeout.find("[DB_TIMEOUT]") == 0, "statement timeout classification");
    require(querydiag::safe_error(Operation::connect, {{"IM002", 0}}).find("[DB_DRIVER]") == 0,
        "missing driver classification");
    require(querydiag::safe_error(Operation::execute, {{"08001", 0}, {"42000", 229}}).find("[DB_PERMISSION]") == 0,
        "read-only permission classification");
    require(querydiag::safe_error(Operation::connect, {{"42000", 4060}}).find("[DB_DATABASE]") == 0,
        "target database classification");
    require(querydiag::safe_error(Operation::read, {{"TEST_PASSWORD\nSQLSTATE=28000", 7}})
        .find("diagnostic=unavailable") != std::string::npos, "malformed SQLSTATE omitted");
    require(querydiag::safe_error(Operation::fetch).find("[DB_FETCH]") == 0, "missing diagnostic fallback");
    require(querydiag::safe_error(Operation::timeout).find("[DB_TIMEOUT_POLICY]") == 0,
        "unsupported timeout attribute has stable code");
    const std::vector<querydiag::Diagnostic> many(100, {"42000", 208});
    const auto bounded = querydiag::safe_error(Operation::execute, many);
    require(bounded.size() < 600, "diagnostic output bounded");

    std::string error, messages, request_id;
    std::vector<int> rows{1, 2};
    {
        querydiag::RequestScope request("synthetic_query", error,
            [&](const std::string& text) { messages += text; });
        request_id = request.id();
        request.observe_rows(rows);
        querydiag::stage_event(Operation::execute);
        querydiag::stage_event(Operation::fetch);
        {
            querydiag::RequestScope nested("synthetic_nested", error, request.log_callback());
            require(nested.id() == request_id, "nested operations share request ID");
            error = querydiag::safe_error(Operation::execute, {{"42000", 208}});
        }
    }
    require(error.find("request_id=" + request_id) != std::string::npos, "error correlated with request");
    require(messages.find("stage=execute") != std::string::npos &&
        messages.find("stage=fetch") != std::string::npos, "execution and fetch phases correlated");
    require(messages.find("event=end outcome=failed code=DB_SCHEMA") != std::string::npos,
        "nested failure code propagated to parent end event");
    require(messages.find("rows=2") != std::string::npos, "result count included");
    require(messages.find("duration_ms=") != std::string::npos, "elapsed time included");
    require(error.find("request_id=", error.find("request_id=") + 1) == std::string::npos,
        "request ID appended once across nested errors");

    std::string next_id, success_log;
    error = "stale failure";
    {
        querydiag::RequestScope request("successful_query", error,
            [&](const std::string& text) { success_log += text; });
        next_id = request.id();
    }
    require(next_id != request_id && error.empty(), "new request independent and stale error cleared");
    require(success_log.find("outcome=completed code=OK") != std::string::npos, "successful end recorded");
    std::string threaded_id, threaded_error;
    std::thread worker([&] {
        querydiag::RequestScope request("parallel_query", threaded_error);
        threaded_id = request.id();
    });
    {
        querydiag::RequestScope request("main_query", error);
        next_id = request.id();
        worker.join();
        require(threaded_id != next_id, "concurrent request IDs isolated");
    }
    std::string exception_log;
    try {
        querydiag::RequestScope request("exception_query", error,
            [&](const std::string& text) { exception_log += text; });
        throw std::runtime_error("TEST_PASSWORD TEST_PATIENT SELECT secret");
    } catch (const std::runtime_error&) {}
    require(exception_log.find("code=DB_EXCEPTION") != std::string::npos, "exception logged without what text");

    // Real production entry points: invalid settings fail locally, without a server.
    search::DbSettings settings;
    settings.password = L"TEST_PASSWORD";
    require(!search::test_database_connection(settings, error), "connection test rejects missing server");
    require(error.find("request_id=") != std::string::npos, "settings connection error correlated");
    search::QueryInput input;
    input.patient_name = "TEST_PATIENT";
    input.barcode = "TEST_BARCODE";
    std::vector<search::TrendPoint> points;
    require(!search::query_trend_points(settings, input, points, error), "trend invalid settings fail safely");
    require(error.find("request_id=") != std::string::npos, "trend error correlated");
#ifdef _WIN32
    const auto collected = querydiag::odbc_error(Operation::execute, SQL_HANDLE_STMT,
        SQL_NULL_HANDLE, &fake_diagnostic);
    require(reader_calls == 2 && collected.find("[DB_TIMEOUT]") == 0,
        "production collector handles warning and end-of-diagnostics");
    require(collected.find("native=-2") != std::string::npos, "negative native diagnostic retained");
#endif
    std::string file_log;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().extension() != ".log") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        file_log += contents.str();
    }
    require(file_log.find("request_id=" + request_id) != std::string::npos,
        "same request persisted in file log");
    const auto output = file_log + messages + exception_log + error + authentication + timeout + bounded;
    for (const char* sensitive : {"TEST_PASSWORD", "TEST_PATIENT", "TEST_BARCODE", "TEST_ID_CARD",
            "LIS_TEST_MISSING_SECRET", "SELECT secret", "PWD=", "SERVER="})
        require(output.find(sensitive) == std::string::npos, "sensitive value absent from logs and errors");
    return true;
}
}  // namespace

int main() {
    namespace fs = std::filesystem;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("lis-log-test-" + std::to_string(suffix));
    fs::create_directory(root);
    applog::init(root.wstring(), 14, 1024 * 1024);
    std::string messages, error;
    std::vector<search::ReportRow> rows;
    search::QueryFilters filters;
    filters.connection_string = "DRIVER={LIS_TEST_MISSING_SECRET};UID=TEST_PATIENT;PWD=TEST_PASSWORD;";
    filters.patient_name = "TEST_PATIENT";
    filters.patient_id = "TEST_ID_CARD";
    filters.barcode = "TEST_BARCODE";
    if (search::query_reports(filters, rows, error,
            [&](const std::string& text) { messages += text; })) return 3;
    std::string initial_file_log;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().extension() != ".log") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        initial_file_log += text.str();
    }
    const auto initial_output = messages + error + initial_file_log;
    for (const auto* secret : {"LIS_TEST_MISSING_SECRET", "TEST_PATIENT", "TEST_PASSWORD",
            "TEST_ID_CARD", "TEST_BARCODE", "PWD="}) {
        if (initial_output.find(secret) != std::string::npos) return 4;
    }
#ifdef _WIN32
    if (messages.find("db connect failed") == std::string::npos) return 5;
#endif
    if (error.find("request_id=") == std::string::npos || error.find("[DB_") != 0) return 6;
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
    applog::init(root.wstring(), 14, 1024 * 1024);
    if (!diagnostic_checks(root)) return 10;
    fs::remove_all(root);
    std::cout << "query diagnostics, privacy, retention and capacity tests passed\n";
}
