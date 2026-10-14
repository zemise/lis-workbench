#pragma once
#ifdef _WIN32
#include "app_settings.h"
#include "login_core.h"
#include <functional>

namespace login {

struct Recommendation {
    Source source;
    Identity identity;
    bool automatic_allowed = false;
};

// Candidates must remain stable across process snapshots and database validation.
std::vector<Recommendation> recommendations(
    const search::DbSettings& settings, const std::wstring& directory,
    const std::function<bool()>& cancelled);

// Startup requires a unique instance. Explicit selection may choose among several.
// Resume permits ordinary modal windows, while source lock detection still applies.
Result adopt_source(
    const search::DbSettings& settings, const std::wstring& directory,
    const Source& source, const std::function<bool()>& cancelled,
    bool require_enabled = true, bool require_unique_instance = false);

} // namespace login
#endif
