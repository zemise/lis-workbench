#pragma once
#include <cstdint>
#include <string>
namespace lis_update {
struct UpdateZipInfo {
    std::uint64_t files = 0;
    std::uint64_t expanded_bytes = 0;
};
bool validate_update_zip(const std::wstring& path, UpdateZipInfo& info, std::string& error);
std::string serialize_update_manifest(const struct UpdateManifest& manifest);
}
