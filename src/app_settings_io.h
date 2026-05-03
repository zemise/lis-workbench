#pragma once

#include "app_settings.h"

#include <filesystem>

namespace search {

std::filesystem::path module_dir();
std::filesystem::path default_ini_path();
AppSettings load_settings(const std::filesystem::path& ini_path);
bool save_settings(const std::filesystem::path& ini_path, const AppSettings& settings);

}  // namespace search
