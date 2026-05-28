#include "update_manifest.h"

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::wstring app_dir;
    std::wstring app_exe = L"lis_workbench.exe";
    std::wstring package_dir;
    std::wstring package_file;
    std::wstring manifest_path;
    DWORD pid = 0;
};

std::wstring join_path(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const wchar_t last = left.back();
    if (last == L'\\' || last == L'/') return left + right;
    return left + L"\\" + right;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring last_error_text(const wchar_t* action) {
    std::wostringstream out;
    out << action << L" failed (win32=" << GetLastError() << L")";
    return out.str();
}

bool is_dot_entry(const wchar_t* name) {
    return std::wcscmp(name, L".") == 0 || std::wcscmp(name, L"..") == 0;
}

bool is_directory(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool file_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring parent_dir(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

bool ensure_directory(const std::wstring& path, std::wstring& error) {
    if (path.empty() || is_directory(path)) return true;
    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        const std::wstring parent = path.substr(0, pos);
        if (!parent.empty() && !ensure_directory(parent, error)) return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    error = last_error_text(L"CreateDirectoryW");
    return false;
}

bool delete_tree(const std::wstring& path, std::wstring& error) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if (DeleteFileW(path.c_str())) return true;
        error = last_error_text(L"DeleteFileW");
        return false;
    }

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = join_path(path, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (is_dot_entry(data.cFileName)) continue;
            if (!delete_tree(join_path(path, data.cFileName), error)) {
                FindClose(find);
                return false;
            }
        } while (FindNextFileW(find, &data));
        const DWORD last = GetLastError();
        FindClose(find);
        if (last != ERROR_NO_MORE_FILES) {
            error = L"directory enumeration failed";
            return false;
        }
    }

    if (RemoveDirectoryW(path.c_str())) return true;
    error = last_error_text(L"RemoveDirectoryW");
    return false;
}

void append_log(const std::wstring& app_dir, const std::wstring& line) {
    const std::wstring log_dir = join_path(app_dir, L"log");
    std::wstring ignored_error;
    ensure_directory(log_dir, ignored_error);

    const std::wstring log_path = join_path(log_dir, L"updater.log");
    HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t prefix[64]{};
    std::swprintf(prefix, 64, L"%04d-%02d-%02d %02d:%02d:%02d ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring text = std::wstring(prefix) + line + L"\r\n";
    const std::string bytes = wide_to_utf8(text);
    DWORD written = 0;
    if (!bytes.empty()) {
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }
    CloseHandle(file);
}

bool wait_for_process(DWORD pid, DWORD timeout_ms, std::wstring& error) {
    if (pid == 0) return true;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return true;
        error = last_error_text(L"OpenProcess");
        return false;
    }
    const DWORD result = WaitForSingleObject(process, timeout_ms);
    CloseHandle(process);
    if (result == WAIT_OBJECT_0) return true;
    error = result == WAIT_TIMEOUT ? L"timed out waiting for application exit"
                                   : last_error_text(L"WaitForSingleObject");
    return false;
}

bool copy_tree(const std::wstring& source, const std::wstring& target,
               const std::vector<std::wstring>& skip_names,
               const std::wstring& running_updater_path,
               std::wstring& error) {
    if (!ensure_directory(target, error)) return false;
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = join_path(source, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        error = last_error_text(L"FindFirstFileW");
        return false;
    }

    do {
        if (is_dot_entry(data.cFileName)) continue;
        const std::wstring name = data.cFileName;
        bool skip = false;
        for (const auto& item : skip_names) {
            if (_wcsicmp(item.c_str(), name.c_str()) == 0) {
                skip = true;
                break;
            }
        }
        if (skip) continue;

        const std::wstring src = join_path(source, name);
        const std::wstring dst = join_path(target, name);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!copy_tree(src, dst, skip_names, running_updater_path, error)) {
                FindClose(find);
                return false;
            }
        } else {
            if (!running_updater_path.empty() &&
                _wcsicmp(dst.c_str(), running_updater_path.c_str()) == 0) {
                continue;
            }
            if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
                error = last_error_text(L"CopyFileW");
                FindClose(find);
                return false;
            }
        }
    } while (FindNextFileW(find, &data));

    const DWORD last = GetLastError();
    FindClose(find);
    if (last != ERROR_NO_MORE_FILES) {
        error = L"directory enumeration failed";
        return false;
    }
    return true;
}

bool directory_snapshot(const std::wstring& path, std::uint64_t& files, std::uint64_t& bytes) {
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = join_path(path, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;

    files = 0;
    bytes = 0;
    do {
        if (is_dot_entry(data.cFileName)) continue;
        const std::wstring child = join_path(path, data.cFileName);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            std::uint64_t child_files = 0;
            std::uint64_t child_bytes = 0;
            if (directory_snapshot(child, child_files, child_bytes)) {
                files += child_files;
                bytes += child_bytes;
            }
        } else {
            ++files;
            bytes += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
                     static_cast<std::uint64_t>(data.nFileSizeLow);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return true;
}

bool extract_zip_with_shell(const std::wstring& zip_path, const std::wstring& target_dir,
                            const std::wstring& required_exe, std::wstring& error) {
    if (!file_exists(zip_path)) {
        error = L"package file does not exist";
        return false;
    }
    if (!delete_tree(target_dir, error) || !ensure_directory(target_dir, error)) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool owns_com = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        error = L"CoInitializeEx failed";
        return false;
    }

    IShellDispatch* shell = nullptr;
    hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IShellDispatch, reinterpret_cast<void**>(&shell));
    if (FAILED(hr) || !shell) {
        if (owns_com) CoUninitialize();
        error = L"CoCreateInstance Shell.Application failed";
        return false;
    }

    VARIANT zip_variant{};
    VARIANT target_variant{};
    VariantInit(&zip_variant);
    VariantInit(&target_variant);
    zip_variant.vt = VT_BSTR;
    zip_variant.bstrVal = SysAllocString(zip_path.c_str());
    target_variant.vt = VT_BSTR;
    target_variant.bstrVal = SysAllocString(target_dir.c_str());

    Folder* zip_folder = nullptr;
    Folder* target_folder = nullptr;
    hr = shell->NameSpace(zip_variant, &zip_folder);
    if (SUCCEEDED(hr) && zip_folder) {
        hr = shell->NameSpace(target_variant, &target_folder);
    }
    VariantClear(&zip_variant);
    VariantClear(&target_variant);
    shell->Release();

    if (FAILED(hr) || !zip_folder || !target_folder) {
        if (zip_folder) zip_folder->Release();
        if (target_folder) target_folder->Release();
        if (owns_com) CoUninitialize();
        error = L"open zip or target folder failed";
        return false;
    }

    FolderItems* items = nullptr;
    hr = zip_folder->Items(&items);
    if (FAILED(hr) || !items) {
        zip_folder->Release();
        target_folder->Release();
        if (owns_com) CoUninitialize();
        error = L"read zip items failed";
        return false;
    }

    VARIANT item_variant{};
    VARIANT option_variant{};
    VariantInit(&item_variant);
    VariantInit(&option_variant);
    item_variant.vt = VT_DISPATCH;
    item_variant.pdispVal = items;
    option_variant.vt = VT_I4;
    option_variant.lVal = 4 | 16 | 512 | 1024;
    hr = target_folder->CopyHere(item_variant, option_variant);

    items->Release();
    zip_folder->Release();
    target_folder->Release();
    if (owns_com) CoUninitialize();

    if (FAILED(hr)) {
        error = L"extract zip failed";
        return false;
    }

    const std::wstring required_path = join_path(target_dir, required_exe);
    std::uint64_t last_files = 0;
    std::uint64_t last_bytes = 0;
    int stable_ticks = 0;
    for (int i = 0; i < 180; ++i) {
        std::uint64_t files = 0;
        std::uint64_t bytes = 0;
        if (file_exists(required_path) && directory_snapshot(target_dir, files, bytes)) {
            if (files == last_files && bytes == last_bytes) {
                ++stable_ticks;
            } else {
                stable_ticks = 0;
                last_files = files;
                last_bytes = bytes;
            }
        }
        if (stable_ticks >= 4) {
            return true;
        }
        Sleep(500);
    }
    error = L"extract zip timed out or package does not contain app exe";
    return false;
}

std::wstring current_module_path() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
    while (len == path.size()) {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
    }
    path.resize(len);
    return path;
}

std::wstring make_backup_dir(const std::wstring& app_dir) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[64]{};
    std::swprintf(name, 64, L"%04d%02d%02d%02d%02d%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return join_path(join_path(app_dir, L"backup"), name);
}

bool parse_args(int argc, wchar_t** argv, Options& options, std::wstring& error) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring key = argv[i];
        auto next = [&]() -> std::wstring {
            if (i + 1 >= argc) return {};
            return argv[++i];
        };
        if (key == L"--app-dir") {
            options.app_dir = next();
        } else if (key == L"--app-exe") {
            options.app_exe = next();
        } else if (key == L"--package-dir") {
            options.package_dir = next();
        } else if (key == L"--package-file") {
            options.package_file = next();
        } else if (key == L"--manifest") {
            options.manifest_path = next();
        } else if (key == L"--pid") {
            options.pid = static_cast<DWORD>(_wtoi(next().c_str()));
        } else {
            error = L"unknown argument: " + key;
            return false;
        }
    }

    if (options.app_dir.empty()) {
        error = L"--app-dir is required";
        return false;
    }
    if (options.package_dir.empty() && options.package_file.empty()) {
        error = L"--package-dir or --package-file is required";
        return false;
    }
    return true;
}

bool validate_manifest_if_present(const Options& options, std::wstring& error) {
    if (options.manifest_path.empty()) return true;
    std::string text;
    std::string parse_error;
    if (!lis_update::read_text_file_utf8(options.manifest_path, text, parse_error)) {
        error = L"manifest read failed: " + std::wstring(parse_error.begin(), parse_error.end());
        return false;
    }
    lis_update::UpdateManifest manifest;
    if (!lis_update::parse_update_manifest_json(text, manifest, parse_error)) {
        error = L"manifest parse failed: " + std::wstring(parse_error.begin(), parse_error.end());
        return false;
    }
    return true;
}

int run_update(const Options& options) {
    append_log(options.app_dir, L"updater started");
    std::wstring error;
    if (!validate_manifest_if_present(options, error)) {
        append_log(options.app_dir, error);
        return 2;
    }
    if (!wait_for_process(options.pid, 60000, error)) {
        append_log(options.app_dir, error);
        return 5;
    }

    std::wstring package_dir = options.package_dir;
    if (package_dir.empty()) {
        const std::wstring base = parent_dir(options.package_file);
        package_dir = join_path(base.empty() ? options.app_dir : base, L"expanded");
        append_log(options.app_dir, L"extracting package file");
        if (!extract_zip_with_shell(options.package_file, package_dir, options.app_exe, error)) {
            append_log(options.app_dir, L"extract failed: " + error);
            return 10;
        }
    }
    if (!is_directory(package_dir)) {
        append_log(options.app_dir, L"package directory does not exist");
        return 3;
    }
    if (!file_exists(join_path(package_dir, options.app_exe))) {
        append_log(options.app_dir, L"package does not contain app exe");
        return 4;
    }

    const std::wstring backup_dir = make_backup_dir(options.app_dir);
    if (!copy_tree(options.app_dir, backup_dir, {L"backup", L"log"}, L"", error)) {
        append_log(options.app_dir, L"backup failed: " + error);
        return 6;
    }

    const std::wstring running_updater = current_module_path();
    if (!copy_tree(package_dir, options.app_dir, {L"ClientConfig.ini", L"log"}, running_updater, error)) {
        append_log(options.app_dir, L"replace failed: " + error);
        std::wstring restore_error;
        if (!copy_tree(backup_dir, options.app_dir, {L"backup", L"log"}, running_updater, restore_error)) {
            append_log(options.app_dir, L"rollback failed: " + restore_error);
            return 8;
        }
        append_log(options.app_dir, L"rollback completed");
        return 7;
    }

    const std::wstring app_path = join_path(options.app_dir, options.app_exe);
    HINSTANCE started = ShellExecuteW(nullptr, L"open", app_path.c_str(), nullptr,
                                      options.app_dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(started) <= 32) {
        append_log(options.app_dir, L"start updated app failed");
        return 9;
    }
    append_log(options.app_dir, L"update completed");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    std::wstring error;
    if (!parse_args(argc, argv, options, error)) {
        if (!options.app_dir.empty()) append_log(options.app_dir, error);
        return 1;
    }
    return run_update(options);
}

#else

#include <iostream>

int main() {
    std::cout << "Updater is only available on Windows.\n";
    return 0;
}

#endif
