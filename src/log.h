#pragma once

#include <string>

namespace applog {

enum class Level { error, warn, info, debug };

void init(const std::wstring& log_dir);

// Thread-safe. Writes timestamped line to the daily log file.
void write(Level level, const char* file, int line, const std::string& message);

}  // namespace applog

// Convenience macros — capture __FILE__ / __LINE__ automatically.
#define LOG_ERROR(msg)   applog::write(applog::Level::error, __FILE__, __LINE__, msg)
#define LOG_WARN(msg)    applog::write(applog::Level::warn,  __FILE__, __LINE__, msg)
#define LOG_INFO(msg)    applog::write(applog::Level::info,  __FILE__, __LINE__, msg)
#define LOG_DEBUG(msg)   applog::write(applog::Level::debug, __FILE__, __LINE__, msg)
