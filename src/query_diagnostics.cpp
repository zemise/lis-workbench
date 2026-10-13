#include "query_diagnostics.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <random>
#include <sstream>
#include <utility>

namespace querydiag {
namespace {
thread_local RequestScope* current = nullptr;

std::string new_id() {
    static const std::uint64_t seed = [] {
        try {
            std::random_device random;
            return (static_cast<std::uint64_t>(random()) << 32) ^ random();
        } catch (...) {
            return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }();
    static std::atomic<std::uint64_t> sequence{0};
    std::ostringstream out;
    out << 'r' << std::hex << seed << '-' << ++sequence;
    return out.str();
}

bool valid_state(const std::string& state) {
    return state.size() == 5 && std::all_of(state.begin(), state.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    });
}

const char* stage_name(Operation operation) {
    switch (operation) {
    case Operation::environment: return "environment";
    case Operation::connection_handle: return "connection_handle";
    case Operation::connect: return "connect";
    case Operation::statement: return "statement";
    case Operation::timeout: return "timeout_policy";
    case Operation::execute: return "execute";
    case Operation::fetch: return "fetch";
    case Operation::read: return "read";
    case Operation::platform: return "platform";
    }
    return "unknown";
}

struct Classification { const char* code; const char* hint; };
Classification classify(Operation operation, const std::vector<Diagnostic>& records) {
    // Pick actionable causes across records; never inspect provider text.
    for (const auto& record : records) {
        if (valid_state(record.sqlstate) && (record.sqlstate == "28000" || record.native_error == 18456)) return {"DB_AUTH", "数据库登录失败，请检查账号和密码。"};
    }
    for (const auto& record : records) {
        if (record.sqlstate == "HYT00" || record.sqlstate == "HYT01" || record.sqlstate == "S1T00")
            return {"DB_TIMEOUT", "数据库操作超时，请检查网络或缩小查询范围后重试。"};
    }
    for (const auto& record : records) {
        if (!valid_state(record.sqlstate)) continue;
        if (record.native_error == 4060) return {"DB_DATABASE", "无法打开目标数据库，请检查数据库名称和访问权限。"};
        if (record.native_error == 229) return {"DB_PERMISSION", "数据库查询权限不足，请联系管理员检查只读权限。"};
        if (record.native_error == 208 || record.native_error == 207)
            return {"DB_SCHEMA", "数据库表或字段不可用，请联系管理员核对数据库结构。"};
        if (record.sqlstate == "IM002" || record.sqlstate == "IM003" || record.sqlstate == "IM014")
            return {"DB_DRIVER", "数据库驱动不可用，请检查驱动安装及程序位数。"};
        if (record.sqlstate == "HY008") return {"DB_CANCELLED", "数据库操作已取消。"};
    }
    for (const auto& record : records) {
        if (valid_state(record.sqlstate) && record.sqlstate.substr(0, 2) == "08")
            return {"DB_CONNECTION", "数据库连接失败，请检查网络、服务器和驱动配置。"};
    }
    switch (operation) {
    case Operation::environment:
    case Operation::connection_handle:
    case Operation::statement: return {"DB_HANDLE", "无法分配数据库资源，请重试。"};
    case Operation::connect: return {"DB_CONNECTION", "数据库连接失败，请检查连接配置。"};
    case Operation::timeout: return {"DB_TIMEOUT_POLICY", "驱动未接受查询超时设置，请检查驱动兼容性。"};
    case Operation::execute: return {"DB_EXECUTE", "数据库查询执行失败，请联系管理员检查查询权限和数据库结构。"};
    case Operation::fetch: return {"DB_FETCH", "数据库结果读取失败，请重试。"};
    case Operation::read: return {"DB_READ", "数据库字段读取失败，请重试。"};
    case Operation::platform: return {"DB_PLATFORM", "此数据库查询仅支持 Windows。"};
    }
    return {"DB_UNKNOWN", "数据库操作失败。"};
}
}  // namespace

std::string safe_error(Operation operation, const std::vector<Diagnostic>& diagnostics) {
    const auto classification = classify(operation, diagnostics);
    std::string summary;
    // Bound malformed/noisy driver diagnostics; no patient/credential text accepted.
    std::size_t count = 0;
    for (const auto& record : diagnostics) {
        if (!valid_state(record.sqlstate)) continue;
        if (count++ == 8) break;
        if (!summary.empty()) summary += ";";
        summary += "SQLSTATE=" + record.sqlstate + ",native=" + std::to_string(record.native_error);
    }
    if (summary.empty()) summary = "diagnostic=unavailable";
    if (current) current->failure(classification.code, stage_name(operation), summary);
    return std::string("[") + classification.code + "] " + classification.hint +
        " (stage=" + stage_name(operation) + "; " + summary + ")";
}

void stage_event(Operation operation) noexcept {
    try {
        if (current) current->event(std::string("event=stage stage=") + stage_name(operation));
    } catch (...) {}
}

#ifdef _WIN32
std::string odbc_error(Operation operation, SQLSMALLINT type, SQLHANDLE handle, DiagnosticReader reader) {
    std::vector<Diagnostic> records;
    for (SQLSMALLINT index = 1; index <= 8; ++index) {
        SQLWCHAR state[6]{};
        SQLWCHAR discarded[1]{};
        SQLINTEGER native = 0;
        SQLSMALLINT length = 0;
        // A one-character output buffer obtains codes while discarding provider text.
        const auto result = reader(type, handle, index, state, &native, discarded, 1, &length);
        if (result != SQL_SUCCESS && result != SQL_SUCCESS_WITH_INFO) break;
        std::string ascii;
        for (int i = 0; i < 5; ++i) {
            const auto c = state[i];
            if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) { ascii.clear(); break; }
            ascii += static_cast<char>(c);
        }
        records.push_back({ascii, static_cast<std::int32_t>(native)});
    }
    return safe_error(operation, records);
}
#endif

RequestScope::RequestScope(const char* query, std::string& error, Sink sink)
    : parent_(current), id_(parent_ ? parent_->id() : new_id()), query_(query),
      error_(error), sink_(std::move(sink)), started_(std::chrono::steady_clock::now()),
      exceptions_(std::uncaught_exceptions()) {
    error_.clear();
    current = this;
    event("event=begin");
}

RequestScope::~RequestScope() noexcept {
    try {
        const bool exception = std::uncaught_exceptions() > exceptions_;
        const bool failed = exception || !error_.empty();
        if (!error_.empty()) {
            // Propagate a nested query's stable code, never its error text to logs.
            for (const char* known : {"DB_AUTH", "DB_TIMEOUT", "DB_DRIVER", "DB_CANCELLED",
                    "DB_CONNECTION", "DB_DATABASE", "DB_PERMISSION", "DB_SCHEMA", "DB_HANDLE",
                    "DB_TIMEOUT_POLICY", "DB_EXECUTE", "DB_FETCH", "DB_READ", "DB_PLATFORM", "DB_UNKNOWN"}) {
                if (error_.rfind(std::string("[") + known + "]", 0) == 0) { code_ = known; break; }
            }
            if (error_.rfind("[DB_", 0) != 0) error_ = "[DB_INPUT] " + error_;
        }
        if (!error_.empty() && error_.find("request_id=" + id_) == std::string::npos)
            error_ += " [request_id=" + id_ + "]";
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_).count();
        event(std::string("event=end outcome=") + (failed ? "failed" : "completed") +
            " code=" + (exception ? "DB_EXCEPTION" : failed ? code_ : "OK") +
            " duration_ms=" + std::to_string(duration) +
            (row_count_ ? " rows=" + std::to_string(row_count_()) : ""));
    } catch (...) {}
    current = parent_;
}

RequestScope::Sink RequestScope::log_callback() {
    return [this](const std::string& message) { event(message); };
}

void RequestScope::event(const std::string& safe_event) noexcept {
    try {
        // A nested query forwarding to its parent's callback is already decorated.
        if (safe_event.rfind("request_id=", 0) == 0) {
            if (sink_) sink_(safe_event);
            return;
        }
        auto message = "request_id=" + id_ + " query=" + query_ + " " + safe_event;
        if (message.empty() || message.back() != '\n') message += '\n';
        if (safe_event.find("event=failure") != std::string::npos ||
            safe_event.find("unavailable:") != std::string::npos) LOG_WARN(message);
        else LOG_DEBUG(message);
        if (sink_) sink_(message);
    } catch (...) { /* Diagnostic sinks cannot change query results. */ }
}

void RequestScope::failure(const std::string& code, const std::string& stage,
                           const std::string& safe_diagnostics) noexcept {
    try {
        code_ = code;
        event("event=failure stage=" + stage + " code=" + code + " " + safe_diagnostics);
    } catch (...) {}
}
}  // namespace querydiag
