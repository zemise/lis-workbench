#include "log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace applog {
namespace {

std::mutex g_mutex;
std::wstring g_log_dir;
bool g_initialized = false;

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

void init(const std::wstring& log_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_log_dir = log_dir;
    // Create directory if needed
    if (!g_log_dir.empty()) {
        CreateDirectoryW(g_log_dir.c_str(), nullptr);
    }
    g_initialized = true;
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
    if (g_initialized) {
        std::wstring path = daily_log_path();
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
