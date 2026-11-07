#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace login {
enum class SourceProtocol { slash_numeric_v1 };
enum class SourceComponent { main_executable, main_window, session_framework, session_helper };
struct SourceFileVersion {
    SourceComponent role;
    std::uint64_t size;
    const char *sha256;
    const char *component;
};
struct SourceVersion {
    const char *name;
    SourceProtocol protocol;
    std::array<SourceFileVersion, 4> files;
};
inline bool source_component_matches(const SourceFileVersion &file, std::uint64_t size,
                                     const std::string &digest) {
    return file.size == size && digest == file.sha256;
}
// Only complete verified component combinations establish an identity. Sizes
// filter candidates before hashing; filenames are not an identity or protocol.
inline constexpr SourceVersion supported_source_versions[] = {
    {"verified-source-v1",
     SourceProtocol::slash_numeric_v1,
     {{{SourceComponent::main_executable, 114688,
        "9dd66c8dcd3cfaf4c4eea3bf858135dc1547e91ae4dfdfe1da097636655c4535", "main_executable"},
       {SourceComponent::main_window, 1822720,
        "41eb00292f6a1944e6ff9299445c9e11c87d2191d8838d36d924e56a8a707faf", "main_window"},
       {SourceComponent::session_framework, 1581056,
        "8cd17a7fb742405397102031f9e37d8ca06a96f2d643639d37ab4244692b3289", "session_framework"},
       {SourceComponent::session_helper, 21504,
        "2217c5be5bab6feb41275b172a730a449e51b1772791c9b23b09eb03cdb9f9c2", "session_helper"}}}}};
inline bool source_size_candidate(SourceComponent role, std::uint64_t size) {
    for (const auto &version : supported_source_versions)
        for (const auto &file : version.files)
            if (file.role == role && file.size == size)
                return true;
    return false;
}
} // namespace login
