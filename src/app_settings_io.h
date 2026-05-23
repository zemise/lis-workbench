#pragma once

#include "app_settings.h"

#include <string>

namespace search {

std::wstring module_dir();
std::wstring default_ini_path();
AppSettings load_settings(const std::wstring& ini_path);
bool save_settings(const std::wstring& ini_path, const AppSettings& settings);

// Module-private config — section = module name, key = setting name
void save_module_int(const wchar_t* module, const wchar_t* key, int value);
void save_module_str(const wchar_t* module, const wchar_t* key, const std::wstring& value);
int  load_module_int(const wchar_t* module, const wchar_t* key, int fallback);
std::wstring load_module_str(const wchar_t* module, const wchar_t* key, const wchar_t* fallback);

}  // namespace search
