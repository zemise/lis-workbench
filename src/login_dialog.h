#pragma once
#ifdef _WIN32
#include "login_core.h"
#include "app_settings.h"
#include <windows.h>
namespace login {
enum class LoginMode { manual, startup, recommend };
std::optional<Identity> show_dialog(HINSTANCE instance, const search::DbSettings& settings,
    LoginMode mode = LoginMode::manual,
    const std::wstring& directory = L"C:\\LIS\\创星医疗管理系统（标准）");
}
#endif
