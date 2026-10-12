#include "update_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace lis_update {
namespace {

std::string trim_copy(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

size_t find_json_key(const std::string& json, const std::string& key, size_t start = 0) {
    const std::string token = "\"" + key + "\"";
    return json.find(token, start);
}

bool extract_json_string(const std::string& json, const std::string& key,
                         std::string& value) {
    const size_t key_pos = find_json_key(json, key);
    if (key_pos == std::string::npos) return false;
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return false;
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;

    std::string out;
    bool escaping = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaping) {
            switch (ch) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(ch); break;
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            value = out;
            return true;
        }
        out.push_back(ch);
    }
    return false;
}

bool extract_json_uint64(const std::string& json, const std::string& key,
                         std::uint64_t& value) {
    const size_t key_pos = find_json_key(json, key);
    if (key_pos == std::string::npos) return false;
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return false;
    ++colon;
    while (colon < json.size() && std::isspace(static_cast<unsigned char>(json[colon]))) {
        ++colon;
    }
    size_t end = colon;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    if (end == colon) return false;
    std::istringstream in(json.substr(colon, end - colon));
    in >> value;
    return !in.fail();
}

std::vector<int> version_parts(const std::string& version) {
    std::vector<int> parts;
    std::string token;
    for (char ch : version) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            token.push_back(ch);
        } else if (!token.empty()) {
            parts.push_back(std::atoi(token.c_str()));
            token.clear();
        }
    }
    if (!token.empty()) {
        parts.push_back(std::atoi(token.c_str()));
    }
    return parts;
}

#ifdef _WIN32
std::string last_error_message(const char* prefix) {
    std::ostringstream out;
    out << prefix << " (win32=" << GetLastError() << ")";
    return out.str();
}
#endif

}  // namespace

bool parse_update_manifest_json(const std::string& json, UpdateManifest& manifest,
                                std::string& error) {
    UpdateManifest parsed;
    extract_json_string(json, "appId", parsed.app_id);
    extract_json_string(json, "version", parsed.version);
    extract_json_string(json, "channel", parsed.channel);
    extract_json_string(json, "minUpdaterVersion", parsed.min_updater_version);
    extract_json_string(json, "publishedAt", parsed.published_at);
    extract_json_string(json, "file", parsed.package.file);
    extract_json_string(json, "sha256", parsed.package.sha256);
    extract_json_uint64(json, "size", parsed.package.size);

    parsed.app_id = trim_copy(parsed.app_id);
    parsed.version = trim_copy(parsed.version);
    parsed.channel = trim_copy(parsed.channel);
    parsed.package.file = trim_copy(parsed.package.file);
    parsed.package.sha256 = lowercase_copy(trim_copy(parsed.package.sha256));

    if (!is_supported_manifest(parsed, error)) {
        return false;
    }
    manifest = parsed;
    error.clear();
    return true;
}

bool is_supported_manifest(const UpdateManifest& manifest, std::string& error) {
    if (manifest.app_id != "lis-workbench") {
        error = "manifest appId is not lis-workbench";
        return false;
    }
    if (!is_valid_release_version(manifest.version)) {
        error = "manifest version must have form vYYYY.MM.DD or vYYYY.MM.DD.N (1-999999999)";
        return false;
    }
    if (manifest.package.file.empty()) {
        error = "manifest package.file is empty";
        return false;
    }
    if (manifest.channel != "stable") {
        error = "unsupported update channel";
        return false;
    }
    if (manifest.min_updater_version != "1.0.0" &&
        manifest.min_updater_version != kUpdaterProtocolVersion) {
        error = "updater protocol is too old; upgrade using the installer";
        return false;
    }
    if (manifest.package.size == 0 || manifest.package.size > kMaxUpdatePackageSize) {
        error = "invalid update package size (maximum 100 MiB)";
        return false;
    }
    if (manifest.package.sha256.size() != 64) {
        error = "manifest package.sha256 must be 64 hex characters";
        return false;
    }
    for (char ch : manifest.package.sha256) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            error = "manifest package.sha256 contains non-hex characters";
            return false;
        }
    }
    error.clear();
    return true;
}

bool is_valid_release_version(const std::string& version) {
    if (version.size() < 11 || version.size() > 21) return false;
    for (size_t i = 0; i < 11; ++i) {
        const char c = version[i];
        if (!(i == 0 ? c == 'v' : i == 5 || i == 8 ? c == '.' : c >= '0' && c <= '9')) return false;
    }
    const int year = std::stoi(version.substr(1, 4));
    const int month = std::stoi(version.substr(6, 2));
    const int day = std::stoi(version.substr(9, 2));
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int days[] = {0,31,leap ? 29 : 28,31,30,31,30,31,31,30,31,30,31};
    if (year == 0 || month < 1 || month > 12 || day < 1 || day > days[month]) return false;
    if (version.size() == 11) return true;
    if (version.size() < 13 || version[11] != '.' || version[12] < '1' || version[12] > '9') return false;
    for (size_t i = 13; i < version.size(); ++i) if (version[i] < '0' || version[i] > '9') return false;
    return true;
}

// These three releases used future dates for updates actually published on Oct 12.
// Normalize only these exact legacy identifiers; never change stored package identity.
static std::string normalize_legacy_release(const std::string& version) {
    if (version == "v2026.10.13") return "v2026.10.12.1";
    if (version == "v2026.10.14") return "v2026.10.12.2";
    if (version == "v2026.10.15") return "v2026.10.12.3";
    return version;
}

int compare_version_strings(const std::string& left, const std::string& right) {
    const auto a = version_parts(normalize_legacy_release(left));
    const auto b = version_parts(normalize_legacy_release(right));
    const size_t count = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < count; ++i) {
        const int av = i < a.size() ? a[i] : 0;
        const int bv = i < b.size() ? b[i] : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

#ifdef _WIN32
bool read_text_file_utf8(const std::wstring& path, std::string& text, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = last_error_message("open file failed");
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        error = "file size is invalid";
        return false;
    }
    text.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = text.empty() ||
        ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        error = last_error_message("read file failed");
        text.clear();
        return false;
    }
    text.resize(read);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    error.clear();
    return true;
}

bool sha256_file_hex(const std::wstring& path, std::string& hex, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = last_error_message("open file failed");
        return false;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(file);
        error = last_error_message("CryptAcquireContext failed");
        return false;
    }
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        CloseHandle(file);
        error = last_error_message("CryptCreateHash failed");
        return false;
    }

    unsigned char buffer[64 * 1024];
    DWORD read = 0;
    BOOL read_ok = FALSE;
    while ((read_ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) && read > 0) {
        if (!CryptHashData(hash, buffer, read, 0)) {
            CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            CloseHandle(file);
            error = last_error_message("CryptHashData failed");
            return false;
        }
    }
    if (!read_ok) {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        CloseHandle(file);
        error = last_error_message("read file failed");
        return false;
    }

    unsigned char digest[32];
    DWORD digest_size = sizeof(digest);
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        CloseHandle(file);
        error = last_error_message("CryptGetHashParam failed");
        return false;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    hex.clear();
    hex.reserve(digest_size * 2);
    for (DWORD i = 0; i < digest_size; ++i) {
        hex.push_back(kHex[(digest[i] >> 4) & 0x0F]);
        hex.push_back(kHex[digest[i] & 0x0F]);
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    CloseHandle(file);
    error.clear();
    return true;
}
#endif

}  // namespace lis_update
