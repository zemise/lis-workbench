#pragma once

#ifdef _WIN32

namespace lis_update {

inline constexpr const wchar_t* kConfigSection = L"Update";
inline constexpr const wchar_t* kSourceFolder = L"Folder";
inline constexpr const wchar_t* kSourceHttp = L"Http";
inline constexpr const wchar_t* kSourceFolderLabel = L"共享文件夹";
inline constexpr const wchar_t* kSourceHttpLabel = L"HTTP";
inline constexpr const wchar_t* kDefaultGithubManifestUrl =
    L"https://github.com/zemise/lis-workbench/releases/latest/download/manifest.json";

}  // namespace lis_update

#endif
