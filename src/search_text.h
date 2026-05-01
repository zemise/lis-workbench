#pragma once

#include <string>

namespace search {

std::string trim(std::string text);
std::string wide_to_utf8(const std::wstring& text);
std::wstring utf8_to_wide(const std::string& text);

}  // namespace search
