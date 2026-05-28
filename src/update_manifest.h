#pragma once

#include <cstdint>
#include <string>

namespace lis_update {

struct UpdatePackageInfo {
    std::string file;
    std::string sha256;
    std::uint64_t size = 0;
};

struct UpdateManifest {
    std::string app_id;
    std::string version;
    std::string channel;
    std::string min_updater_version;
    std::string published_at;
    UpdatePackageInfo package;
};

bool parse_update_manifest_json(const std::string& json, UpdateManifest& manifest,
                                std::string& error);

bool is_supported_manifest(const UpdateManifest& manifest, std::string& error);

int compare_version_strings(const std::string& left, const std::string& right);

#ifdef _WIN32
bool read_text_file_utf8(const std::wstring& path, std::string& text, std::string& error);
bool sha256_file_hex(const std::wstring& path, std::string& hex, std::string& error);
#endif

}  // namespace lis_update
