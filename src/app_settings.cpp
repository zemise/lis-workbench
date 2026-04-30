#include "app_settings.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>

namespace search {

namespace {

std::wstring read_ini(const std::filesystem::path& ini_path, const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t buffer[1024] = {};
    GetPrivateProfileStringW(section, key, fallback, buffer, 1024, ini_path.c_str());
    return buffer;
}

int read_ini_int(const std::filesystem::path& ini_path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini_path.c_str()));
}

bool write_ini(const std::filesystem::path& ini_path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key, value.c_str(), ini_path.c_str()) != FALSE;
}

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
    AppSettings settings;
    settings.db.server = read_ini(ini_path, L"Database", L"Server");
    settings.db.initial_database = read_ini(ini_path, L"Database", L"InitialDatabase");
    settings.db.user = read_ini(ini_path, L"Database", L"User");
    settings.db.password = read_ini(ini_path, L"Database", L"Password");
    settings.ui.font_size = clamp_font_size(read_ini_int(ini_path, L"UI", L"FontSize", settings.ui.font_size));
    settings.ui.splitter_x = read_ini_int(ini_path, L"UI", L"SplitterX", settings.ui.splitter_x);
    return settings;
}

bool save_settings(const std::filesystem::path& ini_path, const AppSettings& settings) {
    bool ok = true;
    ok &= write_ini(ini_path, L"Database", L"Server", settings.db.server);
    ok &= write_ini(ini_path, L"Database", L"InitialDatabase", settings.db.initial_database);
    ok &= write_ini(ini_path, L"Database", L"User", settings.db.user);
    ok &= write_ini(ini_path, L"Database", L"Password", settings.db.password);
    ok &= WritePrivateProfileStringW(L"Database", L"Database", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Database", L"Driver", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"FontSize", std::to_wstring(clamp_font_size(settings.ui.font_size)).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"SplitterX", std::to_wstring(settings.ui.splitter_x).c_str(), ini_path.c_str()) != FALSE;
    return ok;
}

std::wstring build_connection_string_w(const DbSettings& settings) {
    if (settings.server.empty() || settings.initial_database.empty() || settings.user.empty()) {
        return L"";
    }
    return L"packet size=4096;user id=" + settings.user +
           L";password=" + settings.password +
           L";data source=" + settings.server +
           L";persist security info=True;initial catalog=" + settings.initial_database;
}

}  // namespace search

#else

namespace search {

std::filesystem::path module_dir() {
    return {};
}

std::filesystem::path default_ini_path() {
    return "result_search.ini";
}

AppSettings load_settings(const std::filesystem::path&) {
    return {};
}

bool save_settings(const std::filesystem::path&, const AppSettings&) {
    return false;
}

std::wstring build_connection_string_w(const DbSettings&) {
    return L"";
}

}  // namespace search

#endif
