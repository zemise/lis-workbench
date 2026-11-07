#pragma once
#include "login_core.h"
#include "app_settings.h"
#include <functional>
namespace login {
std::optional<std::wstring> lookup_name(const search::DbSettings& settings,
    std::wstring code, const std::function<bool()>& cancelled);
// Account validity only. Callers must verify a live local Source before adoption.
// Account-wide online fields are intentionally independent of the local session.
Result validate_source_account(const search::DbSettings& settings, const Source& source,
    const std::function<bool()>& cancelled);
Result authenticate(const search::DbSettings& settings, std::wstring code,
                    std::wstring password, const std::function<bool()>& cancelled);
}
