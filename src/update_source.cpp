#include "update_source.h"

#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#endif

#include <cstdint>
#include <sstream>
#include <utility>

namespace lis_update {
namespace {

std::string win32_error_message(const char* prefix, unsigned long code) {
    std::ostringstream out;
    out << prefix << " (win32=" << code << ")";
    return out.str();
}

std::wstring package_file_name(const std::string& file) {
    std::string value = file;
    const size_t query = value.find_first_of("?#");
    if (query != std::string::npos) {
        value.resize(query);
    }
    const size_t pos = value.find_last_of("/\\");
    if (pos != std::string::npos) {
        value = value.substr(pos + 1);
    }
    if (value.empty()) {
        value = "update-package.bin";
    }
    return utf8_to_wide_update(value);
}

bool is_absolute_http_url(const std::wstring& url) {
    return url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0;
}

std::wstring base_url_of(const std::wstring& url) {
    const size_t query = url.find_first_of(L"?#");
    const std::wstring clean = query == std::wstring::npos ? url : url.substr(0, query);
    const size_t pos = clean.find_last_of(L"/");
    if (pos == std::wstring::npos) return clean;
    return clean.substr(0, pos + 1);
}

std::wstring resolve_package_url(const std::wstring& manifest_url, const std::string& file) {
    const std::wstring wide_file = utf8_to_wide_update(file);
    if (is_absolute_http_url(wide_file)) {
        return wide_file;
    }
    return base_url_of(manifest_url) + wide_file;
}

#ifdef _WIN32
bool ensure_directory(const std::wstring& path, std::string& error) {
    if (path.empty()) return true;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) return true;
        error = "path exists but is not a directory";
        return false;
    }

    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        const std::wstring parent = path.substr(0, pos);
        if (!parent.empty() && !ensure_directory(parent, error)) {
            return false;
        }
    }
    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    error = win32_error_message("create directory failed", GetLastError());
    return false;
}

bool download_url_to_file(const std::wstring& url, const std::wstring& target_path,
                          std::string& error) {
    const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), target_path.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        error.clear();
        return true;
    }
    std::ostringstream out;
    out << "download failed (hresult=0x" << std::hex << static_cast<unsigned long>(hr) << ")";
    error = out.str();
    return false;
}

bool file_size_bytes(const std::wstring& path, std::uint64_t& size, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = win32_error_message("open file failed", GetLastError());
        return false;
    }
    LARGE_INTEGER value{};
    if (!GetFileSizeEx(file, &value) || value.QuadPart < 0) {
        CloseHandle(file);
        error = win32_error_message("get file size failed", GetLastError());
        return false;
    }
    CloseHandle(file);
    size = static_cast<std::uint64_t>(value.QuadPart);
    return true;
}
#endif

}  // namespace

std::wstring join_update_path(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const wchar_t last = left.back();
    if (last == L'\\' || last == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

std::wstring utf8_to_wide_update(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        &out[0], needed);
    return out;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

FolderUpdateSource::FolderUpdateSource(std::wstring root_path)
    : root_path_(std::move(root_path)) {}

bool FolderUpdateSource::fetch_manifest(UpdateManifest& manifest, std::string& error) {
#ifndef _WIN32
    (void)manifest;
    error = "FolderUpdateSource is only available on Windows";
    return false;
#else
    std::string text;
    const std::wstring manifest_path = join_update_path(root_path_, L"manifest.json");
    if (!read_text_file_utf8(manifest_path, text, error)) {
        return false;
    }
    return parse_update_manifest_json(text, manifest, error);
#endif
}

bool FolderUpdateSource::fetch_package(const UpdateManifest& manifest,
                                       const std::wstring& target_path,
                                       std::string& error) {
#ifndef _WIN32
    (void)manifest;
    (void)target_path;
    error = "FolderUpdateSource is only available on Windows";
    return false;
#else
    const std::wstring source_path = join_update_path(root_path_, utf8_to_wide_update(manifest.package.file));
    if (!CopyFileW(source_path.c_str(), target_path.c_str(), FALSE)) {
        std::ostringstream out;
        out << "copy package failed (win32=" << GetLastError() << ")";
        error = out.str();
        return false;
    }
    return verify_update_package(target_path, manifest, error);
#endif
}

HttpUpdateSource::HttpUpdateSource(std::wstring manifest_url)
    : manifest_url_(std::move(manifest_url)) {}

bool HttpUpdateSource::fetch_manifest(UpdateManifest& manifest, std::string& error) {
#ifndef _WIN32
    (void)manifest;
    error = "HttpUpdateSource is only available on Windows";
    return false;
#else
    wchar_t temp_path[MAX_PATH]{};
    wchar_t temp_file[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp_path) ||
        !GetTempFileNameW(temp_path, L"lis", 0, temp_file)) {
        error = win32_error_message("create temp file failed", GetLastError());
        return false;
    }

    if (!download_url_to_file(manifest_url_, temp_file, error)) {
        DeleteFileW(temp_file);
        return false;
    }

    std::string text;
    if (!read_text_file_utf8(temp_file, text, error)) {
        DeleteFileW(temp_file);
        return false;
    }
    DeleteFileW(temp_file);
    return parse_update_manifest_json(text, manifest, error);
#endif
}

bool HttpUpdateSource::fetch_package(const UpdateManifest& manifest,
                                     const std::wstring& target_path,
                                     std::string& error) {
#ifndef _WIN32
    (void)manifest;
    (void)target_path;
    error = "HttpUpdateSource is only available on Windows";
    return false;
#else
    const std::wstring url = resolve_package_url(manifest_url_, manifest.package.file);
    if (!download_url_to_file(url, target_path, error)) {
        return false;
    }
    return verify_update_package(target_path, manifest, error);
#endif
}

bool verify_update_package(const std::wstring& path, const UpdateManifest& manifest,
                           std::string& error) {
#ifndef _WIN32
    (void)path;
    (void)manifest;
    error = "verify_update_package is only available on Windows";
    return false;
#else
    if (manifest.package.size > 0) {
        std::uint64_t actual_size = 0;
        if (!file_size_bytes(path, actual_size, error)) {
            return false;
        }
        if (actual_size != manifest.package.size) {
            error = "package size mismatch";
            return false;
        }
    }

    std::string actual_hash;
    if (!sha256_file_hex(path, actual_hash, error)) {
        return false;
    }
    if (actual_hash != manifest.package.sha256) {
        error = "package sha256 mismatch";
        return false;
    }
    error.clear();
    return true;
#endif
}

bool check_and_fetch_update(IUpdateSource& source,
                            const std::string& current_version,
                            const std::wstring& cache_dir,
                            UpdateCheckResult& result,
                            std::string& error) {
#ifndef _WIN32
    (void)source;
    (void)current_version;
    (void)cache_dir;
    (void)result;
    error = "check_and_fetch_update is only available on Windows";
    return false;
#else
    UpdateManifest manifest;
    if (!source.fetch_manifest(manifest, error)) {
        return false;
    }

    result = {};
    result.manifest = manifest;
    if (compare_version_strings(manifest.version, current_version) <= 0) {
        error.clear();
        return true;
    }

    if (!ensure_directory(cache_dir, error)) {
        return false;
    }
    const std::wstring target_path = join_update_path(cache_dir, package_file_name(manifest.package.file));
    if (!source.fetch_package(manifest, target_path, error)) {
        return false;
    }

    result.update_available = true;
    result.package_path = target_path;
    error.clear();
    return true;
#endif
}

}  // namespace lis_update
