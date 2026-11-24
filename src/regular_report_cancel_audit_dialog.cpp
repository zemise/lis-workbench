#include "regular_report_cancel_audit_dialog.h"

#ifdef _WIN32

#include "login_database.h"
#include "resource.h"
#include "search_text.h"
#include "window_task.h"

#include <iterator>
#include <utility>

namespace {

struct CancelAuditDialog {
    const search::CancelAuditPreview& preview;
    const search::DbSettings& settings;
    const std::wstring& label;
    const std::wstring& login_code;
    std::int64_t employee_id;
    RegularCancelAuditConfirmation& confirmation;
};

INT_PTR CALLBACK cancelAuditDialogProc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        const auto* state = reinterpret_cast<const CancelAuditDialog*>(lp);
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        SetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_REPORT, state->label.c_str());
        SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_REASON, EM_SETLIMITTEXT, 300, 0);
        SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_PASSWORD, EM_SETLIMITTEXT, 256, 0);
        for (size_t index = 0; index < state->preview.reasons.size(); ++index) {
            const auto& reason = state->preview.reasons[index];
            auto label = login::trim(search::utf8_to_wide(reason.type_name));
            const auto text = login::trim(search::utf8_to_wide(reason.text));
            if (label.empty()) label = text;
            else if (!text.empty() && text != label) label += L"  " + text;
            const auto item = SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_TYPE, CB_ADDSTRING, 0,
                                                  reinterpret_cast<LPARAM>(label.c_str()));
            if (item >= 0 && SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_TYPE, CB_SETITEMDATA,
                                                 item, static_cast<LPARAM>(index)) == CB_ERR)
                SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_TYPE, CB_DELETESTRING, item, 0);
        }
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    const auto* state = reinterpret_cast<const CancelAuditDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    if (LOWORD(wp) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    const bool typeChanged = LOWORD(wp) == IDC_CANCEL_AUDIT_TYPE && HIWORD(wp) == CBN_SELCHANGE;
    if (!typeChanged && LOWORD(wp) != IDOK) return FALSE;
    const auto selected = SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_TYPE, CB_GETCURSEL, 0, 0);
    const auto index = selected == CB_ERR ? CB_ERR
        : SendDlgItemMessageW(dialog, IDC_CANCEL_AUDIT_TYPE, CB_GETITEMDATA, selected, 0);
    const auto* reason = index >= 0 && static_cast<size_t>(index) < state->preview.reasons.size()
        ? &state->preview.reasons[static_cast<size_t>(index)] : nullptr;
    if (typeChanged) {
        if (reason)
            SetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_REASON, search::utf8_to_wide(reason->text).c_str());
        return TRUE;
    }

    wchar_t text[301]{};
    GetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_REASON, text, static_cast<int>(std::size(text)));
    const auto explanation = login::trim(text);
    if (state->preview.reason_required && (!reason || explanation.empty())) {
        MessageBoxW(dialog, L"请选择原因类型并填写取消审核原因。", L"取消审核", MB_ICONINFORMATION);
        return TRUE;
    }

    wchar_t password[257]{};
    GetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_PASSWORD, password, static_cast<int>(std::size(password)));
    SetDlgItemTextW(dialog, IDC_CANCEL_AUDIT_PASSWORD, L"");
    auto secret = std::wstring(password);
    SecureZeroMemory(password, sizeof(password));
    const auto result = login::authenticate(state->settings, state->login_code, std::move(secret), [] {
        return app::window_task_detail::sessionSuspended();
    });
    if (result.status != login::Status::success || !result.identity ||
        result.identity->employee_id != state->employee_id) {
        MessageBoxW(dialog, L"当前账号身份验证失败。", L"取消审核", MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_CANCEL_AUDIT_PASSWORD));
        return TRUE;
    }
    state->confirmation.employee_name = result.identity->name;
    if (reason) state->confirmation.reason = *reason;
    state->confirmation.reason.text = search::wide_to_utf8(explanation);
    EndDialog(dialog, IDOK);
    return TRUE;
}

}  // namespace

RegularCancelAuditDialogResult regularShowCancelAuditDialog(
    HWND owner, const search::CancelAuditPreview& preview, const search::DbSettings& settings,
    const std::wstring& label, const std::wstring& login_code, std::int64_t employee_id,
    RegularCancelAuditConfirmation& confirmation) {
    confirmation = {};
    CancelAuditDialog dialog{preview, settings, label, login_code, employee_id, confirmation};
    const auto choice = DialogBoxParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_REGULAR_REPORT_CANCEL_AUDIT), owner, cancelAuditDialogProc,
        reinterpret_cast<LPARAM>(&dialog));
    if (choice == -1) return RegularCancelAuditDialogResult::failed;
    if (choice == IDOK) return RegularCancelAuditDialogResult::confirmed;
    return RegularCancelAuditDialogResult::cancelled;
}

#endif
