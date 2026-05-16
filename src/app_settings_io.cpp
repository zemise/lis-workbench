#include "app_settings_io.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <system_error>

namespace search {

namespace {

constexpr const wchar_t* CONFIG_FILE_NAME = L"ClientConfig.ini";
constexpr const wchar_t* LEGACY_CONFIG_FILE_NAME = L"result_search.ini";

int clamp_font_size(int value) {
    return std::max(8, std::min(24, value));
}

void migrate_legacy_config_if_needed(const std::filesystem::path& dir) {
    static bool checked = false;
    if (checked) return;
    checked = true;

    const auto current = dir / CONFIG_FILE_NAME;
    const auto legacy = dir / LEGACY_CONFIG_FILE_NAME;
    std::error_code ec;
    if (!std::filesystem::exists(current, ec) && std::filesystem::exists(legacy, ec)) {
        std::filesystem::copy_file(legacy, current, std::filesystem::copy_options::none, ec);
    }
}

}  // namespace

std::filesystem::path module_dir() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path default_ini_path() {
    const auto dir = module_dir();
    migrate_legacy_config_if_needed(dir);
    return dir / CONFIG_FILE_NAME;
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

    auto read_lis_str = [&](const wchar_t* key, const std::wstring& fallback) {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"LisSummary", key, fallback.c_str(), buf, 1024, ini_path.c_str());
        if (buf[0] == L'\0') {
            return fallback;
        }
        return std::wstring(buf);
    };
    s.lis.abo_codes = read_lis_str(L"AboCodes", s.lis.abo_codes);
    s.lis.rhd_codes = read_lis_str(L"RhdCodes", s.lis.rhd_codes);
    s.lis.hgb_codes = read_lis_str(L"HgbCodes", s.lis.hgb_codes);
    s.lis.plt_codes = read_lis_str(L"PltCodes", s.lis.plt_codes);
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
    ok &= WritePrivateProfileStringW(L"LisSummary", L"AboCodes", s.lis.abo_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"RhdCodes", s.lis.rhd_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"HgbCodes", s.lis.hgb_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"PltCodes", s.lis.plt_codes.c_str(), ini_path.c_str()) != FALSE;
    return ok;
}

void save_module_int(const wchar_t* module, const wchar_t* key, int value) {
    WritePrivateProfileStringW(module, key, std::to_wstring(value).c_str(),
                               default_ini_path().c_str());
}

void save_module_str(const wchar_t* module, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(module, key, value.c_str(),
                               default_ini_path().c_str());
}

int load_module_int(const wchar_t* module, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(module, key, fallback,
                                                  default_ini_path().c_str()));
}

std::wstring load_module_str(const wchar_t* module, const wchar_t* key, const wchar_t* fallback) {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(module, key, fallback, buf, 1024,
                             default_ini_path().c_str());
    return std::wstring(buf);
}

}  // namespace search

#endif
