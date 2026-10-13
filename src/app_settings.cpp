#include "app_settings.h"
#include "odbc_connection_policy.h"

namespace search {
std::wstring build_connection_string_w(const DbSettings& settings) {
    return odbc::query_connection_string(settings);
}
}  // namespace search
