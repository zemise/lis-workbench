#pragma once
#ifdef _WIN32
#include "login_core.h"
#include "app_settings.h"
#include <windows.h>
namespace login {
std::optional<Identity> show_dialog(HINSTANCE instance, const search::DbSettings& settings);
}
#endif
