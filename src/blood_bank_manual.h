#pragma once

// Shared identity for the virtual report view and its text-only label.
// Database queries and report associations keep the physical instrument code.
namespace blood_manual {

inline constexpr const char* view_code = "666";
inline constexpr const char* source_code = "626";
inline constexpr const char* name = "血库手工";
inline constexpr const char* abbreviation = "XKSG";
inline constexpr const char* group_name = "PRP";

}  // namespace blood_manual
