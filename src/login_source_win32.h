#pragma once
#ifdef _WIN32
#include "login_source.h"
#include <functional>

namespace login {

// Read-only, restricted to the verified installation and current Windows user/session.
// An incomplete scan cannot establish an identity or resume business activity.
SourceScan scan_sources(const std::wstring& directory,
                        const std::function<bool()>& cancelled);

} // namespace login
#endif
