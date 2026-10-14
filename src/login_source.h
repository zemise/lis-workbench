#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace login {
struct Source {
    std::uint32_t main_pid = 0, helper_pid = 0;
    std::uint64_t main_created = 0, helper_created = 0, main_hwnd = 0;
    std::int64_t employee_id = 0;
    int system_id = 0, dept_id = 0, port = 0;
    std::wstring machine;
    bool main_enabled = true;
};
struct SourceMain {
    std::uint32_t pid = 0;
    std::uint64_t created = 0;
    bool locked = false;
};
struct SourceScan {
    bool complete = false;
    std::vector<SourceMain> mains;
    std::vector<Source> sources;
    std::vector<std::uint32_t> unreadable_mains;
};
enum class SourceStatus { ready, locked, exited, changed, unavailable };
struct SourceMatch {
    SourceStatus status = SourceStatus::unavailable;
    std::optional<Source> source;
};
// Arguments only, excluding argv[0]. No credentials are accepted or retained.
bool parse_source_arguments(const std::vector<std::wstring>& args, Source& out);
bool same_instance(const Source& a, const Source& b);
bool same_observation(const Source& a, const Source& b);
SourceMatch match_source(const SourceScan& scan, const Source& bound);
}
