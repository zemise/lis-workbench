#pragma once

#ifdef _WIN32

#include "app_settings.h"
#include "search_core.h"

#include <windows.h>
#include <cstdint>

enum class RegularCancelAuditDialogResult { confirmed, cancelled, failed };

struct RegularCancelAuditConfirmation {
    search::CancelAuditReason reason;
    std::wstring employee_name;
};

RegularCancelAuditDialogResult regularShowCancelAuditDialog(
    HWND owner, const search::CancelAuditPreview& preview, const search::DbSettings& settings,
    const std::wstring& label, const std::wstring& login_code, std::int64_t employee_id,
    RegularCancelAuditConfirmation& confirmation);

#endif
