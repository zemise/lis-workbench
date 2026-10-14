#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
#endif

// Query diagnostics only: this library is not linked into Updater/update_core.
namespace querydiag {

enum class Operation { environment, connection_handle, connect, statement, timeout, execute, fetch, read, platform };
struct Diagnostic {
    std::string sqlstate;
    std::int32_t native_error = 0;
};

// Accepts only SQLSTATE and numeric codes. Provider message text is never formatted.
std::string safe_error(Operation operation, const std::vector<Diagnostic>& diagnostics = {});
void stage_event(Operation operation) noexcept;

#ifdef _WIN32
using DiagnosticReader = decltype(&SQLGetDiagRecW);
std::string odbc_error(Operation operation, SQLSMALLINT type, SQLHANDLE handle,
                       DiagnosticReader reader = &SQLGetDiagRecW);
#endif

class RequestScope {
public:
    using Sink = std::function<void(const std::string&)>;
    RequestScope(const char* query, std::string& error, Sink sink = {});
    ~RequestScope() noexcept;
    RequestScope(const RequestScope&) = delete;
    RequestScope& operator=(const RequestScope&) = delete;
    Sink log_callback();
    const std::string& id() const { return id_; }
    template <class Rows> void observe_rows(const Rows& rows) {
        row_count_ = [&rows] { return rows.size(); };
    }
    void event(const std::string& safe_event) noexcept;
    void failure(const std::string& code, const std::string& stage,
                 const std::string& safe_diagnostics) noexcept;
private:
    RequestScope* parent_;
    std::string id_, query_, code_ = "DB_INPUT";
    std::string& error_;
    Sink sink_;
    std::function<std::size_t()> row_count_;
    std::chrono::steady_clock::time_point started_;
    int exceptions_;
};

}  // namespace querydiag
