#include "app_settings_io.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>

namespace search {

namespace {

int clamp_font_size(int value) {
    return std::max(8, std::min(24, value));
}

}  // namespace

std::filesystem::path module_dir() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path default_ini_path() {
    return module_dir() / L"result_search.ini";
}

AppSettings load_settings(const std::filesystem::path& ini_path) {
    AppSettings s;
    auto read_str = [&](const wchar_t* key, const wchar_t* fallback = L"") {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"Database", key, fallback, buf, 1024, ini_path.c_str());
        return std::wstring(buf);
    };
    auto read_int = [&](const wchar_t* section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini_path.c_str()));
    };
    s.db.server = read_str(L"Server");
    s.db.initial_database = read_str(L"InitialDatabase");
    s.db.user = read_str(L"User");
    s.db.password = read_str(L"Password");
    s.ui.font_size = clamp_font_size(read_int(L"UI", L"FontSize", s.ui.font_size));
    s.ui.splitter_x = read_int(L"UI", L"SplitterX", s.ui.splitter_x);
    return s;
}

bool save_settings(const std::filesystem::path& ini_path, const AppSettings& s) {
    auto write_str = [&](const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(L"Database", key, value.c_str(), ini_path.c_str()) != FALSE;
    };
    bool ok = true;
    ok &= write_str(L"Server", s.db.server);
    ok &= write_str(L"InitialDatabase", s.db.initial_database);
    ok &= write_str(L"User", s.db.user);
    ok &= write_str(L"Password", s.db.password);
    ok &= WritePrivateProfileStringW(L"Database", L"Database", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Database", L"Driver", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"FontSize", std::to_wstring(clamp_font_size(s.ui.font_size)).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"SplitterX", std::to_wstring(s.ui.splitter_x).c_str(), ini_path.c_str()) != FALSE;
    return ok;
}

}  // namespace search

#endif
