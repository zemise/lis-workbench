#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace login {
struct Identity {
    std::int64_t user_id = 0;
    std::int64_t employee_id = 0;
    std::wstring code;
    std::wstring name;
};
enum class Status { success, invalid_credentials, locked, temporary_locked, expired, failed };
struct Result { Status status = Status::failed; std::optional<Identity> identity; };
std::wstring trim(const std::wstring& text);
bool valid_code(const std::wstring& code);
bool decrypt_legacy(const std::wstring& stored, std::wstring& plaintext);
bool password_matches(const std::wstring& stored, const std::wstring& input);
bool parse_lock_seconds(const std::wstring& value, int& seconds);
const wchar_t* message(Status status);
}
