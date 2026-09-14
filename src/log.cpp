#include "log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>
#include <ctime>
#include <mutex>
#include <string>

namespace applog {
namespace {

std::mutex g_mutex;
std::wstring g_log_dir;
bool g_initialized = false;
unsigned g_days = 14;
unsigned long long g_capacity = 20 * 1024 * 1024;

// Only manage our daily files. Never follow links or remove unrelated files.
bool reserve_log_space(size_t incoming) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::directory_entry> files;
    unsigned long long total = 0;
    fs::directory_iterator it(g_log_dir, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        const auto& entry = *it;
        const auto name = entry.path().filename().string();
        if (name.size() != 14 || name.substr(10) != ".log") continue;
        bool daily = true;
        for (size_t i = 0; i < 10; ++i) {
            if (i == 4 || i == 7) daily &= name[i] == '-';
            else daily &= name[i] >= '0' && name[i] <= '9';
        }
        if (!daily || entry.is_symlink(ec) || !entry.is_regular_file(ec)) continue;
        const auto modified = entry.last_write_time(ec);
        if (ec) return false;
        if (fs::file_time_type::clock::now() - modified >= std::chrono::hours(24ULL * g_days)) {
            fs::remove(entry.path(), ec);
            if (ec) return false;
        } else {
            total += entry.file_size(ec);
            if (ec) return false;
            files.push_back(entry);
        }
    }
    if (ec || incoming > g_capacity) return false;
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path() < b.path();
    });
    for (const auto& entry : files) {
        if (total <= g_capacity - incoming) break;
        const auto size = entry.file_size(ec);
        if (ec) return false;
        fs::remove(entry.path(), ec);
        if (ec) return false;
        total -= size;
    }
    return true;
}

const char* level_name(Level lv) {
    switch (lv) {
        case Level::error: return "ERROR";
        case Level::warn:  return "WARN ";
        case Level::info:  return "INFO ";
        case Level::debug: return "DEBUG";
    }
    return "?????";
}

std::wstring daily_log_path() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[64]{};
    swprintf_s(name, L"%04u-%02u-%02u.log", st.wYear, st.wMonth, st.wDay);
    if (g_log_dir.empty()) return name;
    return g_log_dir + L"\\" + name;
}

}  // namespace

void init(const std::wstring& log_dir, unsigned retention_days, unsigned long long max_bytes) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_log_dir = log_dir;
    g_days = (std::max)(1U, (std::min)(365U, retention_days));
    g_capacity = (std::max)(4096ULL, max_bytes);
    // Create directory if needed
    if (!g_log_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(g_log_dir, ec);
    }
    g_initialized = true;
    reserve_log_space(0);
}

void write(Level level, const char* file, int line, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char timestamp[28]{};
    snprintf(timestamp, sizeof(timestamp),
             "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    // Build the log line: [timestamp] [LEVEL] file:line message
    char line_buf[4096]{};
    snprintf(line_buf, sizeof(line_buf), "[%s] [%s] %s:%d %s\n",
             timestamp, level_name(level), file, line, message.c_str());

    // Write to daily log file
    if (g_initialized && reserve_log_space(std::char_traits<char>::length(line_buf))) {
        std::wstring path = daily_log_path();
        std::error_code ec;
        if (std::filesystem::is_symlink(path, ec)) return;
        FILE* f = _wfopen(path.c_str(), L"ab");
        if (f) {
            fputs(line_buf, f);
            fclose(f);
        }
    }

    // Also emit to debugger
    OutputDebugStringA(line_buf);
}

}  // namespace applog
