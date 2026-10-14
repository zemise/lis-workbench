#pragma once
#include <string>

#ifdef _WIN32

namespace lis_update {

inline constexpr const wchar_t* kConfigSection = L"Update";
inline constexpr const wchar_t* kSourceLis = L"LIS";
inline constexpr const wchar_t* kSourceLisLabel = L"LIS";
inline constexpr const wchar_t* kSourceFolder = L"Folder";
inline constexpr const wchar_t* kSourceHttp = L"Http";
inline constexpr const wchar_t* kSourceFolderLabel = L"共享文件夹";
inline constexpr const wchar_t* kSourceHttpLabel = L"HTTP";
inline constexpr const wchar_t* kDefaultGithubManifestUrl =
    L"https://github.com/zemise/lis-workbench/releases/latest/download/manifest.json";

inline std::wstring normalize_source_type(const std::wstring& value) {
    return value == kSourceHttp || value == kSourceFolder ? value : kSourceLis;
}

}  // namespace lis_update

#endif
